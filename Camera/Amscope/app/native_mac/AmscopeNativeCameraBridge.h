#pragma once

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <dispatch/dispatch.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, AmscopeNativeCameraState) {
    AmscopeNativeCameraStateClosed = 0,
    AmscopeNativeCameraStateOpen,
    AmscopeNativeCameraStateStarting,
    AmscopeNativeCameraStateStreaming,
    AmscopeNativeCameraStateStopping,
    AmscopeNativeCameraStateFaulted
};

@interface AmscopeCameraDevice : NSObject
@property(nonatomic, copy, readonly) NSString *deviceID;
@property(nonatomic, copy, readonly) NSString *displayName;
@property(nonatomic, copy, readonly) NSString *modelName;
@property(nonatomic, copy, readonly, nullable) NSString *manufacturer;
@property(nonatomic, copy, readonly, nullable) NSString *model;
@property(nonatomic, copy, readonly, nullable) NSString *serialNumber;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

/// Retains the immutable C++ frame lease for as long as Core Graphics may
/// access its pixel bytes. newCGImage returns a retained CGImage whose data
/// provider owns an additional reference to this lease object.
@interface AmscopeFrameLease : NSObject {
@private
    void *_frameStorage;
}
@property(nonatomic, readonly) uint32_t width;
@property(nonatomic, readonly) uint32_t height;
@property(nonatomic, readonly) uint32_t strideBytes;
@property(nonatomic, readonly) NSUInteger byteCount;
@property(nonatomic, readonly) NSInteger pixelFormat;
- (nullable CGImageRef)newCGImage CF_RETURNS_RETAINED;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

typedef void (^AmscopeCameraListCompletion)(
    NSArray<AmscopeCameraDevice *> *devices,
    NSError * _Nullable error,
    uint64_t requestID,
    uint64_t sessionGeneration);

typedef void (^AmscopeCameraLifecycleCompletion)(
    NSError * _Nullable error,
    uint64_t requestID,
    uint64_t sessionGeneration);

@interface AmscopeNativeCameraBridge : NSObject {
@private
    void *_controllerStorage;
}

/// These blocks are always invoked on the main thread. Frame notifications are
/// coalesced by the C++ latest-frame mailbox; they are not emitted per frame.
@property(nonatomic, copy, nullable) dispatch_block_t onFrameAvailable;
@property(nonatomic, copy, nullable) void (^onAcquisitionError)(NSError *error);

@property(nonatomic, readonly) AmscopeNativeCameraState state;
@property(nonatomic, readonly) uint64_t droppedFrames;
@property(nonatomic, readonly) uint64_t framesPulled;
@property(nonatomic, readonly) uint64_t imageEventsReceived;

- (void)enumerateCamerasWithCompletion:(AmscopeCameraListCompletion)completion;
- (void)openDeviceID:(NSString *)deviceID
          completion:(AmscopeCameraLifecycleCompletion)completion;
- (void)closeWithCompletion:(AmscopeCameraLifecycleCompletion)completion;
- (void)startWithCompletion:(AmscopeCameraLifecycleCompletion)completion;
- (void)stopWithCompletion:(AmscopeCameraLifecycleCompletion)completion;

/// Transfers the newest available frame lease. Returns nil when no frame is
/// pending. The pixel bytes are not copied.
- (nullable AmscopeFrameLease *)takeLatestFrame;

@end

NS_ASSUME_NONNULL_END
