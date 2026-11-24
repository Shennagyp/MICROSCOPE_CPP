#import "AmscopeNativeCameraBridge.h"

#import "NativeCameraController.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace {

NSString *stringFromUtf8(const std::string& value) {
    NSString *result = [[NSString alloc]
        initWithBytes:value.data()
               length:value.size()
             encoding:NSUTF8StringEncoding];
    return result ?: @"";
}

NSString *optionalStringFromUtf8(
    const std::optional<std::string>& value) {
    return value ? stringFromUtf8(*value) : nil;
}

NSError *errorFromCameraError(const amscope::CameraError& error) {
    NSMutableDictionary *userInfo = [NSMutableDictionary dictionary];
    userInfo[NSLocalizedDescriptionKey] =
        error.message.empty() ? @"Unknown camera error."
                              : stringFromUtf8(error.message);
    if (!error.operation.empty())
        userInfo[@"operation"] = stringFromUtf8(error.operation);
    return [NSError errorWithDomain:@"com.shennagyp.amscope.camera"
                               code:error.vendorCode
                           userInfo:userInfo];
}

NSError *requestRejectedError(NSString *operation) {
    return [NSError errorWithDomain:@"com.shennagyp.amscope.camera"
                               code:0
                           userInfo:@{
        NSLocalizedDescriptionKey:
            [NSString stringWithFormat:@"%@ request was rejected because the "
             "native camera command queue is full or shutting down.", operation]
    }];
}

static void releaseFrameProviderData(
    void *info, const void *data, size_t size) {
    (void)data;
    (void)size;
    id retainedLease = (__bridge_transfer id)info;
    (void)retainedLease;
}

const camera::Frame *frameFromStorage(void *storage) {
    if (!storage)
        return nullptr;
    const auto *lease = static_cast<const camera::FramePtr *>(storage);
    return *lease ? lease->get() : nullptr;
}

} // namespace

@interface AmscopeCameraDevice ()
@property(nonatomic, copy, readwrite) NSString *deviceID;
@property(nonatomic, copy, readwrite) NSString *displayName;
@property(nonatomic, copy, readwrite) NSString *modelName;
@property(nonatomic, copy, readwrite, nullable) NSString *manufacturer;
@property(nonatomic, copy, readwrite, nullable) NSString *model;
@property(nonatomic, copy, readwrite, nullable) NSString *serialNumber;
- (instancetype)initWithCameraInfo:(const amscope::CameraInfo&)info;
@end

@implementation AmscopeCameraDevice

- (instancetype)initWithCameraInfo:(const amscope::CameraInfo&)info {
    self = [super init];
    if (!self)
        return nil;

    _deviceID = [stringFromUtf8(info.id) copy];
    _displayName = [stringFromUtf8(info.displayName) copy];
    _modelName = [stringFromUtf8(info.modelName) copy];
    _manufacturer = [optionalStringFromUtf8(info.identity.manufacturer) copy];
    _model = [optionalStringFromUtf8(info.identity.model) copy];
    _serialNumber = [optionalStringFromUtf8(info.identity.serialNumber) copy];
    return self;
}

@end

@interface AmscopeFrameLease ()
- (instancetype)initWithFrame:(camera::FramePtr)frame;
@end

@implementation AmscopeFrameLease

- (instancetype)initWithFrame:(camera::FramePtr)frame {
    self = [super init];
    if (!self)
        return nil;

    _frameStorage = new (std::nothrow) camera::FramePtr(std::move(frame));
    if (!_frameStorage)
        return nil;
    return self;
}

- (void)dealloc {
    delete static_cast<camera::FramePtr *>(_frameStorage);
    _frameStorage = nullptr;
}

- (uint32_t)width {
    const camera::Frame *frame = frameFromStorage(_frameStorage);
    return frame ? frame->width : 0;
}

- (uint32_t)height {
    const camera::Frame *frame = frameFromStorage(_frameStorage);
    return frame ? frame->height : 0;
}

- (uint32_t)strideBytes {
    const camera::Frame *frame = frameFromStorage(_frameStorage);
    return frame ? frame->strideBytes : 0;
}

- (NSUInteger)byteCount {
    const camera::Frame *frame = frameFromStorage(_frameStorage);
    return frame ? static_cast<NSUInteger>(frame->byteSize()) : 0;
}

- (NSInteger)pixelFormat {
    const camera::Frame *frame = frameFromStorage(_frameStorage);
    return frame ? static_cast<NSInteger>(frame->pixelFormat)
                 : static_cast<NSInteger>(camera::PixelFormat::Unknown);
}

- (CGImageRef)newCGImage {
    const camera::Frame *frame = frameFromStorage(_frameStorage);
    if (!frame || !frame->data() || frame->width == 0 || frame->height == 0)
        return nullptr;

    std::uint64_t bytesPerPixel = 0;
    std::size_t bitsPerComponent = 0;
    std::size_t bitsPerPixel = 0;
    CGColorSpaceRef colorSpace = nullptr;
    CGBitmapInfo bitmapInfo = kCGImageAlphaNone;

    switch (frame->pixelFormat) {
    case camera::PixelFormat::RGB24:
        bytesPerPixel = 3;
        bitsPerComponent = 8;
        bitsPerPixel = 24;
        colorSpace = CGColorSpaceCreateDeviceRGB();
        break;
    case camera::PixelFormat::Raw8:
        bytesPerPixel = 1;
        bitsPerComponent = 8;
        bitsPerPixel = 8;
        colorSpace = CGColorSpaceCreateDeviceGray();
        break;
    case camera::PixelFormat::Raw16:
        bytesPerPixel = 2;
        bitsPerComponent = 16;
        bitsPerPixel = 16;
        bitmapInfo = static_cast<CGBitmapInfo>(
            kCGImageAlphaNone | kCGBitmapByteOrder16Little);
        colorSpace = CGColorSpaceCreateDeviceGray();
        break;
    case camera::PixelFormat::Unknown:
        return nullptr;
    }

    const std::uint64_t minimumRowBytes =
        static_cast<std::uint64_t>(frame->width) * bytesPerPixel;
    const std::uint64_t requiredBytes =
        static_cast<std::uint64_t>(frame->strideBytes) * frame->height;
    const bool validBuffer =
        bytesPerPixel != 0 &&
        frame->strideBytes >= minimumRowBytes &&
        requiredBytes <= frame->byteSize();

    if (!validBuffer || !colorSpace) {
        if (colorSpace)
            CGColorSpaceRelease(colorSpace);
        return nullptr;
    }

    // The provider retains this lease until Core Graphics releases the
    // provider. No intermediate pixel buffer or NSData is created.
    void *providerInfo = (__bridge_retained void *)self;
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        providerInfo,
        frame->data(),
        frame->byteSize(),
        releaseFrameProviderData);

    if (!provider) {
        id retainedLease = (__bridge_transfer id)providerInfo;
        (void)retainedLease;
        CGColorSpaceRelease(colorSpace);
        return nullptr;
    }

    CGImageRef image = CGImageCreate(
        frame->width,
        frame->height,
        bitsPerComponent,
        bitsPerPixel,
        frame->strideBytes,
        colorSpace,
        bitmapInfo,
        provider,
        nullptr,
        false,
        kCGRenderingIntentDefault);

    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
    return image;
}

@end

@interface AmscopeNativeCameraBridge ()
- (amscope::native_mac::NativeCameraController *)controller;
@end

@implementation AmscopeNativeCameraBridge

- (instancetype)init {
    self = [super init];
    if (!self)
        return nil;

    __weak AmscopeNativeCameraBridge *weakSelf = self;
    auto *controller = new (std::nothrow) amscope::native_mac::NativeCameraController(
        [weakSelf]() {
            AmscopeNativeCameraBridge *strongSelf = weakSelf;
            if (!strongSelf)
                return;
            dispatch_async(dispatch_get_main_queue(), ^{
                AmscopeNativeCameraBridge *target = strongSelf;
                if (target.onFrameAvailable)
                    target.onFrameAvailable();
            });
        },
        [weakSelf](std::uint64_t, const amscope::CameraError& error) {
            NSError *nativeError = errorFromCameraError(error);
            AmscopeNativeCameraBridge *strongSelf = weakSelf;
            if (!strongSelf)
                return;
            dispatch_async(dispatch_get_main_queue(), ^{
                AmscopeNativeCameraBridge *target = strongSelf;
                if (target.onAcquisitionError &&
                    target.state == AmscopeNativeCameraStateFaulted) {
                    target.onAcquisitionError(nativeError);
                }
            });
        });

    if (!controller)
        return nil;
    _controllerStorage = controller;
    return self;
}

- (void)dealloc {
    delete static_cast<amscope::native_mac::NativeCameraController *>(
        _controllerStorage);
    _controllerStorage = nullptr;
}

- (amscope::native_mac::NativeCameraController *)controller {
    return static_cast<amscope::native_mac::NativeCameraController *>(
        _controllerStorage);
}

- (AmscopeNativeCameraState)state {
    if (![self controller])
        return AmscopeNativeCameraStateClosed;
    return static_cast<AmscopeNativeCameraState>([self controller]->state());
}

- (uint64_t)droppedFrames {
    return [self controller] ? [self controller]->droppedFrames() : 0;
}

- (uint64_t)framesPulled {
    return [self controller] ? [self controller]->framesPulled() : 0;
}

- (uint64_t)imageEventsReceived {
    return [self controller] ? [self controller]->imageEventsReceived() : 0;
}

- (void)enumerateCamerasWithCompletion:(AmscopeCameraListCompletion)completion {
    if (!completion)
        return;

    AmscopeCameraListCompletion completionCopy = [completion copy];
    __weak AmscopeNativeCameraBridge *weakSelf = self;
    const auto requestID = [self controller]->requestEnumerateCameras(
        [weakSelf, completionCopy](
            std::uint64_t completedRequestID,
            std::uint64_t generation,
            amscope::native_mac::NativeCameraController::CameraListResult result) {
            NSMutableArray<AmscopeCameraDevice *> *devices =
                [NSMutableArray array];
            NSError *error = nil;

            if (const auto *failure =
                    std::get_if<amscope::CameraError>(&result)) {
                error = errorFromCameraError(*failure);
            } else if (const auto *cameraList =
                           std::get_if<std::vector<amscope::CameraInfo>>(&result)) {
                for (const auto& info : *cameraList) {
                    AmscopeCameraDevice *device =
                        [[AmscopeCameraDevice alloc] initWithCameraInfo:info];
                    if (device)
                        [devices addObject:device];
                }
            }

            dispatch_async(dispatch_get_main_queue(), ^{
                AmscopeNativeCameraBridge *target = weakSelf;
                if (target)
                    completionCopy(devices, error, completedRequestID, generation);
            });
        });

    if (requestID == 0) {
        NSError *error = requestRejectedError(@"Camera enumeration");
        dispatch_async(dispatch_get_main_queue(), ^{
            completionCopy(@[], error, 0, 0);
        });
    }
}

- (void)openDeviceID:(NSString *)deviceID
          completion:(AmscopeCameraLifecycleCompletion)completion {
    if (!completion)
        return;

    AmscopeCameraLifecycleCompletion completionCopy = [completion copy];
    NSData *identifier = [deviceID dataUsingEncoding:NSUTF8StringEncoding];
    if (!identifier) {
        dispatch_async(dispatch_get_main_queue(), ^{
            completionCopy(requestRejectedError(@"Open camera"), 0, 0);
        });
        return;
    }

    const std::string nativeID(
        static_cast<const char *>(identifier.bytes), identifier.length);
    __weak AmscopeNativeCameraBridge *weakSelf = self;
    const auto requestID = [self controller]->requestOpen(
        nativeID,
        [weakSelf, completionCopy](
            std::uint64_t completedRequestID,
            std::uint64_t generation,
            amscope::native_mac::NativeCameraController::LifecycleResult result) {
            NSError *error = nullptr;
            if (const auto *failure =
                    std::get_if<amscope::CameraError>(&result))
                error = errorFromCameraError(*failure);

            dispatch_async(dispatch_get_main_queue(), ^{
                AmscopeNativeCameraBridge *target = weakSelf;
                if (target)
                    completionCopy(error, completedRequestID, generation);
            });
        });

    if (requestID == 0) {
        NSError *error = requestRejectedError(@"Open camera");
        dispatch_async(dispatch_get_main_queue(), ^{
            completionCopy(error, 0, 0);
        });
    }
}

- (void)closeWithCompletion:(AmscopeCameraLifecycleCompletion)completion {
    if (!completion)
        return;
    AmscopeCameraLifecycleCompletion completionCopy = [completion copy];
    __weak AmscopeNativeCameraBridge *weakSelf = self;
    const auto requestID = [self controller]->requestClose(
        [weakSelf, completionCopy](
            std::uint64_t completedRequestID,
            std::uint64_t generation,
            amscope::native_mac::NativeCameraController::LifecycleResult result) {
            NSError *error = nullptr;
            if (const auto *failure =
                    std::get_if<amscope::CameraError>(&result))
                error = errorFromCameraError(*failure);
            dispatch_async(dispatch_get_main_queue(), ^{
                AmscopeNativeCameraBridge *target = weakSelf;
                if (target)
                    completionCopy(error, completedRequestID, generation);
            });
        });
    if (requestID == 0) {
        NSError *error = requestRejectedError(@"Close camera");
        dispatch_async(dispatch_get_main_queue(), ^{
            completionCopy(error, 0, 0);
        });
    }
}

- (void)startWithCompletion:(AmscopeCameraLifecycleCompletion)completion {
    if (!completion)
        return;
    AmscopeCameraLifecycleCompletion completionCopy = [completion copy];
    __weak AmscopeNativeCameraBridge *weakSelf = self;
    const auto requestID = [self controller]->requestStart(
        [weakSelf, completionCopy](
            std::uint64_t completedRequestID,
            std::uint64_t generation,
            amscope::native_mac::NativeCameraController::LifecycleResult result) {
            NSError *error = nullptr;
            if (const auto *failure =
                    std::get_if<amscope::CameraError>(&result))
                error = errorFromCameraError(*failure);
            dispatch_async(dispatch_get_main_queue(), ^{
                AmscopeNativeCameraBridge *target = weakSelf;
                if (target)
                    completionCopy(error, completedRequestID, generation);
            });
        });
    if (requestID == 0) {
        NSError *error = requestRejectedError(@"Start acquisition");
        dispatch_async(dispatch_get_main_queue(), ^{
            completionCopy(error, 0, 0);
        });
    }
}

- (void)stopWithCompletion:(AmscopeCameraLifecycleCompletion)completion {
    if (!completion)
        return;
    AmscopeCameraLifecycleCompletion completionCopy = [completion copy];
    __weak AmscopeNativeCameraBridge *weakSelf = self;
    const auto requestID = [self controller]->requestStop(
        [weakSelf, completionCopy](
            std::uint64_t completedRequestID,
            std::uint64_t generation,
            amscope::native_mac::NativeCameraController::LifecycleResult result) {
            NSError *error = nullptr;
            if (const auto *failure =
                    std::get_if<amscope::CameraError>(&result))
                error = errorFromCameraError(*failure);
            dispatch_async(dispatch_get_main_queue(), ^{
                AmscopeNativeCameraBridge *target = weakSelf;
                if (target)
                    completionCopy(error, completedRequestID, generation);
            });
        });
    if (requestID == 0) {
        NSError *error = requestRejectedError(@"Stop acquisition");
        dispatch_async(dispatch_get_main_queue(), ^{
            completionCopy(error, 0, 0);
        });
    }
}

- (AmscopeFrameLease *)takeLatestFrame {
    if (![self controller])
        return nil;
    camera::FramePtr frame = [self controller]->takeLatestFrame();
    if (!frame)
        return nil;
    return [[AmscopeFrameLease alloc] initWithFrame:std::move(frame)];
}

@end
