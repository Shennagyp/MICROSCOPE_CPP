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
/// access its pixel bytes. createCGImage returns a retained CGImage whose data
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
- (nullable CGImageRef)createCGImage CF_RETURNS_RETAINED NS_SWIFT_NAME(createCGImage());
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end


typedef NS_ENUM(NSInteger, AmscopeCameraFeatureKind) {
    AmscopeCameraFeatureKindBoolean = 0,
    AmscopeCameraFeatureKindInteger,
    AmscopeCameraFeatureKindIntegerVector,
    AmscopeCameraFeatureKindFloatingPoint,
    AmscopeCameraFeatureKindString,
    AmscopeCameraFeatureKindEnumeration
};

typedef NS_ENUM(NSInteger, AmscopeCameraFeatureGroup) {
    AmscopeCameraFeatureGroupGeneral = 0,
    AmscopeCameraFeatureGroupExposure,
    AmscopeCameraFeatureGroupImageFormat,
    AmscopeCameraFeatureGroupImageProcessing,
    AmscopeCameraFeatureGroupColor,
    AmscopeCameraFeatureGroupTrigger,
    AmscopeCameraFeatureGroupSensor,
    AmscopeCameraFeatureGroupTransport,
    AmscopeCameraFeatureGroupHardware
};

@interface AmscopeCameraFeatureDescriptor : NSObject
@property(nonatomic, copy, readonly) NSString *name;
@property(nonatomic, copy, readonly) NSString *displayName;
@property(nonatomic, readonly) AmscopeCameraFeatureKind kind;
@property(nonatomic, readonly) AmscopeCameraFeatureGroup group;
@property(nonatomic, copy, readonly, nullable) NSString *unit;
@property(nonatomic, readonly) BOOL isAvailable;
@property(nonatomic, readonly) BOOL isReadable;
@property(nonatomic, readonly) BOOL isWritable;
@property(nonatomic, copy, readonly) NSArray<NSString *> *enumerationValues;
@property(nonatomic, copy, readonly) NSArray<NSNumber *> *discreteIntegerValues;
@property(nonatomic, readonly) BOOL hasElementCount;
@property(nonatomic, readonly) NSUInteger elementCount;
@property(nonatomic, copy, readonly, nullable) NSString *disabledReason;
@property(nonatomic, readonly) BOOL hasNumericRange;
@property(nonatomic, readonly) BOOL integerRange;
@property(nonatomic, readonly) int64_t minimumInteger;
@property(nonatomic, readonly) int64_t maximumInteger;
@property(nonatomic, readonly) int64_t integerIncrement;
@property(nonatomic, readonly) BOOL hasIntegerIncrement;
@property(nonatomic, readonly) double minimumFloatingPoint;
@property(nonatomic, readonly) double maximumFloatingPoint;
@property(nonatomic, readonly) double floatingPointIncrement;
@property(nonatomic, readonly) BOOL hasFloatingPointIncrement;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

/// A typed representation of the backend-neutral CameraFeatureValue variant.
@interface AmscopeCameraFeatureValue : NSObject
@property(nonatomic, readonly) AmscopeCameraFeatureKind kind;
@property(nonatomic, readonly) BOOL booleanValue;
@property(nonatomic, readonly) int64_t integerValue;
@property(nonatomic, readonly) double floatingPointValue;
@property(nonatomic, copy, readonly) NSArray<NSNumber *> *integerVectorValue;
@property(nonatomic, copy, readonly) NSString *stringValue;
@property(nonatomic, copy, readonly) NSString *enumerationSymbol;

+ (instancetype)booleanFeatureValue:(BOOL)value;
+ (instancetype)integerFeatureValue:(int64_t)value;
+ (instancetype)integerVectorFeatureValue:(NSArray<NSNumber *> *)values;
+ (instancetype)floatingPointFeatureValue:(double)value;
+ (instancetype)stringFeatureValue:(NSString *)value;
+ (instancetype)enumerationFeatureValue:(NSString *)symbol;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

typedef void (^AmscopeFeatureDescriptorsCompletion)(
    NSArray<AmscopeCameraFeatureDescriptor *> *descriptors,
    NSError * _Nullable error,
    uint64_t requestID,
    uint64_t sessionGeneration,
    uint64_t featureStateGeneration);

typedef void (^AmscopeFeatureReadCompletion)(
    AmscopeCameraFeatureValue * _Nullable value,
    NSError * _Nullable error,
    uint64_t requestID,
    uint64_t sessionGeneration,
    uint64_t featureStateGeneration);

typedef void (^AmscopeFeatureWriteCompletion)(
    NSError * _Nullable error,
    uint64_t requestID,
    uint64_t sessionGeneration);

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

- (void)enumerateCamerasWithCompletion:(AmscopeCameraListCompletion)completion
    NS_SWIFT_NAME(enumerateCameras(completion:));
- (void)openDeviceID:(NSString *)deviceID
          completion:(AmscopeCameraLifecycleCompletion)completion
    NS_SWIFT_NAME(openDeviceID(_:completion:));
- (void)closeWithCompletion:(AmscopeCameraLifecycleCompletion)completion
    NS_SWIFT_NAME(close(completion:));
- (void)startWithCompletion:(AmscopeCameraLifecycleCompletion)completion
    NS_SWIFT_NAME(start(completion:));
- (void)stopWithCompletion:(AmscopeCameraLifecycleCompletion)completion
    NS_SWIFT_NAME(stop(completion:));

- (void)requestFeatureDescriptorsWithCompletion:
    (AmscopeFeatureDescriptorsCompletion)completion
    NS_SWIFT_NAME(requestFeatureDescriptors(completion:));
- (void)readFeatureNamed:(NSString *)name
              completion:(AmscopeFeatureReadCompletion)completion
    NS_SWIFT_NAME(readFeatureNamed(_:completion:));
- (void)writeFeatureNamed:(NSString *)name
                    value:(AmscopeCameraFeatureValue *)value
               completion:(AmscopeFeatureWriteCompletion)completion
    NS_SWIFT_NAME(writeFeatureNamed(_:value:completion:));
- (void)changePreviewResolution:(NSString *)resolutionSymbol
                     completion:(AmscopeFeatureWriteCompletion)completion
    NS_SWIFT_NAME(changePreviewResolution(_:completion:));


/// Transfers the newest available frame lease. Returns nil when no frame is
/// pending. The pixel bytes are not copied.
- (nullable AmscopeFrameLease *)takeLatestFrame;

@end

NS_ASSUME_NONNULL_END
