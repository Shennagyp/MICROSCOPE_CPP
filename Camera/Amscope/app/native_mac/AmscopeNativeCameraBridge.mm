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


NSError *errorFromFeatureAccessError(
    const camera::CameraFeatureAccessError& error) {
    NSMutableDictionary *userInfo = [NSMutableDictionary dictionary];
    userInfo[NSLocalizedDescriptionKey] =
        error.message.empty() ? @"Camera feature request failed."
                              : stringFromUtf8(error.message);
    if (!error.feature.empty())
        userInfo[@"feature"] = stringFromUtf8(error.feature);
    return [NSError errorWithDomain:@"com.shennagyp.amscope.feature"
                               code:static_cast<NSInteger>(error.code)
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


@interface AmscopeCameraFeatureDescriptor ()
@property(nonatomic, copy, readwrite) NSString *name;
@property(nonatomic, copy, readwrite) NSString *displayName;
@property(nonatomic, readwrite) AmscopeCameraFeatureKind kind;
@property(nonatomic, readwrite) AmscopeCameraFeatureGroup group;
@property(nonatomic, copy, readwrite, nullable) NSString *unit;
@property(nonatomic, readwrite) BOOL isAvailable;
@property(nonatomic, readwrite) BOOL isReadable;
@property(nonatomic, readwrite) BOOL isWritable;
@property(nonatomic, copy, readwrite) NSArray<NSString *> *enumerationValues;
@property(nonatomic, copy, readwrite) NSArray<NSNumber *> *discreteIntegerValues;
@property(nonatomic, readwrite) BOOL hasElementCount;
@property(nonatomic, readwrite) NSUInteger elementCount;
@property(nonatomic, copy, readwrite, nullable) NSString *disabledReason;
@property(nonatomic, readwrite) BOOL hasNumericRange;
@property(nonatomic, readwrite) BOOL integerRange;
@property(nonatomic, readwrite) int64_t minimumInteger;
@property(nonatomic, readwrite) int64_t maximumInteger;
@property(nonatomic, readwrite) int64_t integerIncrement;
@property(nonatomic, readwrite) BOOL hasIntegerIncrement;
@property(nonatomic, readwrite) double minimumFloatingPoint;
@property(nonatomic, readwrite) double maximumFloatingPoint;
@property(nonatomic, readwrite) double floatingPointIncrement;
@property(nonatomic, readwrite) BOOL hasFloatingPointIncrement;
- (instancetype)initWithDescriptor:(const camera::CameraFeatureDescriptor&)descriptor;
@end

@implementation AmscopeCameraFeatureDescriptor

- (instancetype)initWithDescriptor:(const camera::CameraFeatureDescriptor&)descriptor {
    self = [super init];
    if (!self)
        return nil;

    _name = [stringFromUtf8(descriptor.name) copy];
    _displayName = [stringFromUtf8(descriptor.displayName) copy];
    _kind = static_cast<AmscopeCameraFeatureKind>(descriptor.kind);
    _group = static_cast<AmscopeCameraFeatureGroup>(descriptor.group);
    _unit = descriptor.unit ? [stringFromUtf8(*descriptor.unit) copy] : nil;
    _isAvailable = descriptor.isAvailable;
    _isReadable = descriptor.isReadable;
    _isWritable = descriptor.isWritable;
    _disabledReason = descriptor.disabledReason
        ? [stringFromUtf8(*descriptor.disabledReason) copy]
        : nil;

    NSMutableArray<NSString *> *enumerations = [NSMutableArray array];
    for (const auto& item : descriptor.enumerationValues)
        [enumerations addObject:stringFromUtf8(item)];
    _enumerationValues = [enumerations copy];

    NSMutableArray<NSNumber *> *discreteValues = [NSMutableArray array];
    for (const auto item : descriptor.discreteIntegerValues)
        [discreteValues addObject:@(item)];
    _discreteIntegerValues = [discreteValues copy];

    _hasElementCount = descriptor.elementCount.has_value();
    _elementCount = descriptor.elementCount.value_or(0);

    if (descriptor.numericRange) {
        _hasNumericRange = YES;
        if (const auto *range =
                std::get_if<camera::CameraIntegerRange>(&*descriptor.numericRange)) {
            _integerRange = YES;
            _minimumInteger = range->minimum;
            _maximumInteger = range->maximum;
            _hasIntegerIncrement = range->increment.has_value();
            _integerIncrement = range->increment.value_or(0);
        } else if (const auto *range =
                       std::get_if<camera::CameraFloatingPointRange>(
                           &*descriptor.numericRange)) {
            _integerRange = NO;
            _minimumFloatingPoint = range->minimum;
            _maximumFloatingPoint = range->maximum;
            _hasFloatingPointIncrement = range->increment.has_value();
            _floatingPointIncrement = range->increment.value_or(0.0);
        } else {
            _hasNumericRange = NO;
        }
    }
    return self;
}

@end

@interface AmscopeCameraFeatureValue ()
@property(nonatomic, readwrite) AmscopeCameraFeatureKind kind;
@property(nonatomic, readwrite) BOOL booleanValue;
@property(nonatomic, readwrite) int64_t integerValue;
@property(nonatomic, readwrite) double floatingPointValue;
@property(nonatomic, copy, readwrite) NSArray<NSNumber *> *integerVectorValue;
@property(nonatomic, copy, readwrite) NSString *stringValue;
@property(nonatomic, copy, readwrite) NSString *enumerationSymbol;
- (instancetype)initWithKind:(AmscopeCameraFeatureKind)kind;
- (instancetype)initWithCameraValue:(const camera::CameraFeatureValue&)value;
@end

@implementation AmscopeCameraFeatureValue

- (instancetype)initWithKind:(AmscopeCameraFeatureKind)kind {
    self = [super init];
    if (!self)
        return nil;
    _kind = kind;
    _integerVectorValue = @[];
    _stringValue = @"";
    _enumerationSymbol = @"";
    return self;
}

- (instancetype)initWithCameraValue:(const camera::CameraFeatureValue&)value {
    self = [super init];
    if (!self)
        return nil;

    std::visit([self](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) {
            _kind = AmscopeCameraFeatureKindBoolean;
            _booleanValue = item;
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            _kind = AmscopeCameraFeatureKindInteger;
            _integerValue = item;
        } else if constexpr (std::is_same_v<T, std::vector<std::int64_t>>) {
            _kind = AmscopeCameraFeatureKindIntegerVector;
            NSMutableArray<NSNumber *> *values = [NSMutableArray array];
            for (const auto entry : item)
                [values addObject:@(entry)];
            _integerVectorValue = [values copy];
        } else if constexpr (std::is_same_v<T, double>) {
            _kind = AmscopeCameraFeatureKindFloatingPoint;
            _floatingPointValue = item;
        } else if constexpr (std::is_same_v<T, std::string>) {
            _kind = AmscopeCameraFeatureKindString;
            _stringValue = [stringFromUtf8(item) copy];
        } else if constexpr (std::is_same_v<T, camera::FeatureEnumeration>) {
            _kind = AmscopeCameraFeatureKindEnumeration;
            _enumerationSymbol = [stringFromUtf8(item.symbol) copy];
        }
    }, value);
    if (!_integerVectorValue)
        _integerVectorValue = @[];
    if (!_stringValue)
        _stringValue = @"";
    if (!_enumerationSymbol)
        _enumerationSymbol = @"";
    return self;
}

+ (instancetype)booleanFeatureValue:(BOOL)value {
    AmscopeCameraFeatureValue *result =
        [[self alloc] initWithKind:AmscopeCameraFeatureKindBoolean];
    result.booleanValue = value;
    return result;
}

+ (instancetype)integerFeatureValue:(int64_t)value {
    AmscopeCameraFeatureValue *result =
        [[self alloc] initWithKind:AmscopeCameraFeatureKindInteger];
    result.integerValue = value;
    return result;
}

+ (instancetype)integerVectorFeatureValue:(NSArray<NSNumber *> *)values {
    AmscopeCameraFeatureValue *result =
        [[self alloc] initWithKind:AmscopeCameraFeatureKindIntegerVector];
    result.integerVectorValue = values;
    return result;
}

+ (instancetype)floatingPointFeatureValue:(double)value {
    AmscopeCameraFeatureValue *result =
        [[self alloc] initWithKind:AmscopeCameraFeatureKindFloatingPoint];
    result.floatingPointValue = value;
    return result;
}

+ (instancetype)stringFeatureValue:(NSString *)value {
    AmscopeCameraFeatureValue *result =
        [[self alloc] initWithKind:AmscopeCameraFeatureKindString];
    result.stringValue = value;
    return result;
}

+ (instancetype)enumerationFeatureValue:(NSString *)symbol {
    AmscopeCameraFeatureValue *result =
        [[self alloc] initWithKind:AmscopeCameraFeatureKindEnumeration];
    result.enumerationSymbol = symbol;
    return result;
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
