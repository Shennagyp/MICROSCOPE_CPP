import AppKit
import Combine
import CoreGraphics
import Foundation
import ImageIO

struct NativeCaptureItem: Identifiable, Equatable {
    let id: String
    let fileURL: URL
    let slide: NativeSlideID
    let capturedAt: Date
    let pixelWidth: Int
    let pixelHeight: Int
    var thumbnailData: Data?

    var dimensionsText: String {
        "\(pixelWidth) × \(pixelHeight)"
    }
}

@MainActor
final class NativeCaptureGalleryStore: ObservableObject {
    @Published private(set) var captures: [NativeCaptureItem] = []
    @Published private(set) var isCapturing = false
    @Published private(set) var captureErrorMessage: String?
    @Published private(set) var isLoadingCaptures = false

    private let maximumVisibleCaptureCount = 80
    private let maximumConcurrentCaptures = 1
    private var pendingCaptureCount = 0
    private var hasLoadedExistingCaptures = false
    private var isLoadingExistingCaptures = false
    private var thumbnailLoadsInProgress: Set<String> = []

    private let directoryURL: URL = {
        let pictures = FileManager.default.urls(
            for: .picturesDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Pictures", isDirectory: true)
        return pictures.appendingPathComponent(
            "AmScope Captures",
            isDirectory: true
        )
    }()

    init() {}

    /// Materializes the gallery listing on first use. The store retains the
    /// resulting items for the application lifetime; reopening the dock does
    /// not rescan persistence.
    func activate() {
        guard !hasLoadedExistingCaptures && !isLoadingExistingCaptures else { return }
        isLoadingExistingCaptures = true
        isLoadingCaptures = true
        loadExistingCaptures()
    }

    /// Loads a thumbnail only when its row becomes visible. Once loaded, the
    /// thumbnail remains part of the resident in-memory item.
    func ensureThumbnailLoaded(for item: NativeCaptureItem) {
        guard item.thumbnailData == nil,
              !thumbnailLoadsInProgress.contains(item.id) else { return }

        thumbnailLoadsInProgress.insert(item.id)
        let fileURL = item.fileURL
        let itemID = item.id

        DispatchQueue.global(qos: .utility).async { [weak self] in
            let thumbnailData = NativeCaptureFileCodec.loadThumbnail(at: fileURL)
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.thumbnailLoadsInProgress.remove(itemID)
                guard let thumbnailData,
                      let index = self.captures.firstIndex(where: { $0.id == itemID }),
                      self.captures[index].thumbnailData == nil else { return }
                self.captures[index].thumbnailData = thumbnailData
            }
        }
    }

    func capture(_ image: CGImage, for slide: NativeSlideID) {
        guard pendingCaptureCount < maximumConcurrentCaptures else {
            captureErrorMessage = "A capture is already being saved. Try again when it finishes."
            return
        }

        pendingCaptureCount += 1
        isCapturing = true
        captureErrorMessage = nil

        let outputDirectory = directoryURL
        let filename = "\(UUID().uuidString)-\(slide.rawValue).png"
        let outputURL = outputDirectory.appendingPathComponent(filename)
        let captureDate = Date()
        let captureSource = NativeCaptureImageSource(image: image)

        // Only the explicit user capture references this source image. The
        // worker detaches its pixels, clears the source holder, and then does
        // PNG compression and thumbnail generation without retaining the
        // camera frame-pool lease as gallery history.
        DispatchQueue.global(qos: .userInitiated).async { [weak self, captureSource] in
            let result = NativeCaptureFileCodec.writeCapture(
                from: captureSource,
                to: outputURL,
                slide: slide,
                capturedAt: captureDate
            )
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.pendingCaptureCount = max(0, self.pendingCaptureCount - 1)
                self.isCapturing = self.pendingCaptureCount > 0

                switch result {
                case .success(let item):
                    self.retainCapture(item)
                case .failure(let error):
                    self.captureErrorMessage = error.localizedDescription
                }
            }
        }
    }

    func delete(_ item: NativeCaptureItem) {
        do {
            try FileManager.default.removeItem(at: item.fileURL)
            captures.removeAll { $0.id == item.id }
        } catch {
            captureErrorMessage = "Could not delete capture: \(error.localizedDescription)"
        }
    }

    func reveal(_ item: NativeCaptureItem) {
        NSWorkspace.shared.activateFileViewerSelecting([item.fileURL])
    }

    func revealCaptureFolder() {
        do {
            try FileManager.default.createDirectory(
                at: directoryURL,
                withIntermediateDirectories: true,
                attributes: nil
            )
            NSWorkspace.shared.open(directoryURL)
        } catch {
            captureErrorMessage = "Could not open the capture folder: \(error.localizedDescription)"
        }
    }

    func dismissCaptureError() {
        captureErrorMessage = nil
    }

    private func retainCapture(_ item: NativeCaptureItem) {
        // A capture can finish while the initial directory listing is in
        // flight. Replace the metadata-only entry rather than retaining two
        // in-memory objects for the same persisted file identity.
        captures.removeAll { $0.id == item.id }
        captures.insert(item, at: 0)
        captures.sort { $0.capturedAt > $1.capturedAt }
        trimGallery()
    }

    private func trimGallery() {
        // Limit in-memory gallery rows/thumbnails only. Captures are user data:
        // old PNG files are never removed automatically by an age/count policy.
        guard captures.count > maximumVisibleCaptureCount else { return }
        captures = Array(captures.prefix(maximumVisibleCaptureCount))
    }

    private func loadExistingCaptures() {
        let directory = directoryURL
        let maximumCount = maximumVisibleCaptureCount

        DispatchQueue.global(qos: .utility).async { [weak self] in
            let loadedItems: [NativeCaptureItem]
            let loadFailure: String?
            do {
                try FileManager.default.createDirectory(
                    at: directory,
                    withIntermediateDirectories: true,
                    attributes: nil
                )
                let urls = try FileManager.default.contentsOfDirectory(
                    at: directory,
                    includingPropertiesForKeys: [.creationDateKey, .contentModificationDateKey],
                    options: [.skipsHiddenFiles]
                )

                // Read each file's date metadata once, then sort the in-memory
                // tuples. The list load does not decode image pixels.
                let datedPNGURLs = urls.compactMap { url -> (url: URL, date: Date)? in
                    guard url.pathExtension.lowercased() == "png" else { return nil }
                    let values = try? url.resourceValues(forKeys: [
                        .creationDateKey,
                        .contentModificationDateKey
                    ])
                    let date = values?.creationDate ?? values?.contentModificationDate ?? .distantPast
                    return (url, date)
                }.sorted { $0.date > $1.date }

                // Keep all capture files on disk. Materialize metadata for the
                // visible subset; thumbnails are decoded only as rows appear.
                loadedItems = datedPNGURLs.prefix(maximumCount).compactMap {
                    NativeCaptureFileCodec.loadCaptureMetadata(
                        at: $0.url,
                        capturedAt: $0.date
                    )
                }
                loadFailure = nil
            } catch {
                loadedItems = []
                loadFailure = error.localizedDescription
            }

            DispatchQueue.main.async {
                guard let self = self else { return }
                let currentIDs = Set(self.captures.map(\.id))
                let merged = self.captures + loadedItems.filter { !currentIDs.contains($0.id) }
                self.captures = merged.sorted { $0.capturedAt > $1.capturedAt }
                self.trimGallery()
                self.isLoadingExistingCaptures = false
                self.isLoadingCaptures = false

                if let loadFailure {
                    self.captureErrorMessage = "Could not load captures: \(loadFailure)"
                } else {
                    self.hasLoadedExistingCaptures = true
                }
            }
        }
    }
}

// The image reference is transferred exactly once under NSLock. CGImage is immutable;
// the lock protects the holder's mutable optional reference across the capture worker.
private final class NativeCaptureImageSource: @unchecked Sendable {
    private let lock = NSLock()
    private var image: CGImage?

    init(image: CGImage) {
        self.image = image
    }

    func takeImage() -> CGImage? {
        lock.lock()
        defer { lock.unlock() }
        let image = self.image
        self.image = nil
        return image
    }
}

private enum NativeCaptureFileCodec {
    static func writeCapture(
        from source: NativeCaptureImageSource,
        to url: URL,
        slide: NativeSlideID,
        capturedAt: Date
    ) -> Result<NativeCaptureItem, NSError> {
        var liveImage = source.takeImage()
        guard let independentImage = copyPixelsAndRelease(&liveImage) else {
            return .failure(makeError("Could not detach the image from the live camera frame."))
        }

        return writeIndependentCapture(
            independentImage,
            to: url,
            slide: slide,
            capturedAt: capturedAt
        )
    }

    private static func copyPixelsAndRelease(_ source: inout CGImage?) -> CGImage? {
        guard let image = source else { return nil }
        let copy = copyPixels(of: image)
        source = nil
        return copy
    }

    private static func writeIndependentCapture(
        _ image: CGImage,
        to url: URL,
        slide: NativeSlideID,
        capturedAt: Date
    ) -> Result<NativeCaptureItem, NSError> {
        do {
            try FileManager.default.createDirectory(
                at: url.deletingLastPathComponent(),
                withIntermediateDirectories: true,
                attributes: nil
            )
        } catch {
            return .failure(error as NSError)
        }

        guard let destination = CGImageDestinationCreateWithURL(
            url as CFURL,
            "public.png" as CFString,
            1,
            nil
        ) else {
            return .failure(makeError("Could not create the capture image file."))
        }

        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else {
            try? FileManager.default.removeItem(at: url)
            return .failure(makeError("The camera image could not be encoded as PNG."))
        }

        // Build the gallery item from the image already in memory. Do not
        // reopen the PNG that was just written merely to create its thumbnail.
        let thumbnailData = makeThumbnail(from: image).flatMap {
            encodeThumbnail($0)
        }

        // The durable capture is successful even if its preview thumbnail
        // cannot be produced now. The row can retry thumbnail loading lazily.
        return .success(NativeCaptureItem(
            id: url.lastPathComponent,
            fileURL: url,
            slide: slide,
            capturedAt: capturedAt,
            pixelWidth: image.width,
            pixelHeight: image.height,
            thumbnailData: thumbnailData
        ))
    }

    static func loadCaptureMetadata(
        at url: URL,
        capturedAt: Date
    ) -> NativeCaptureItem? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let properties = CGImageSourceCopyPropertiesAtIndex(
                source,
                0,
                nil
              ) as? [CFString: Any],
              let width = properties[kCGImagePropertyPixelWidth] as? Int,
              let height = properties[kCGImagePropertyPixelHeight] as? Int,
              width > 0,
              height > 0 else {
            return nil
        }

        let slide = url.lastPathComponent.hasSuffix("-slide2.png") ? .slide2 : .slide1
        return NativeCaptureItem(
            id: url.lastPathComponent,
            fileURL: url,
            slide: slide,
            capturedAt: capturedAt,
            pixelWidth: width,
            pixelHeight: height,
            thumbnailData: nil
        )
    }

    static func loadThumbnail(at url: URL) -> Data? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let thumbnail = CGImageSourceCreateThumbnailAtIndex(
                source,
                0,
                [
                    kCGImageSourceCreateThumbnailFromImageAlways: true,
                    kCGImageSourceCreateThumbnailWithTransform: true,
                    kCGImageSourceThumbnailMaxPixelSize: 320
                ] as CFDictionary
              ) else {
            return nil
        }
        return encodeThumbnail(thumbnail)
    }

    private static func makeThumbnail(from image: CGImage) -> CGImage? {
        let scale = min(
            1,
            320 / CGFloat(max(image.width, image.height))
        )
        let width = max(1, Int((CGFloat(image.width) * scale).rounded(.down)))
        let height = max(1, Int((CGFloat(image.height) * scale).rounded(.down)))
        let colorSpace = CGColorSpaceCreateDeviceRGB()

        guard let context = CGContext(
            data: nil,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else {
            return nil
        }

        context.interpolationQuality = .high
        context.draw(
            image,
            in: CGRect(x: 0, y: 0, width: width, height: height)
        )
        return context.makeImage()
    }

    private static func encodeThumbnail(_ thumbnail: CGImage) -> Data? {
        let data = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(
            data,
            "public.png" as CFString,
            1,
            nil
        ) else {
            return nil
        }
        CGImageDestinationAddImage(destination, thumbnail, nil)
        guard CGImageDestinationFinalize(destination) else { return nil }
        return data as Data
    }

    private static func copyPixels(of image: CGImage) -> CGImage? {
        guard let sourceProvider = image.dataProvider,
              let copiedData = sourceProvider.data,
              let independentProvider = CGDataProvider(data: copiedData),
              let colorSpace = image.colorSpace else {
            return nil
        }

        return CGImage(
            width: image.width,
            height: image.height,
            bitsPerComponent: image.bitsPerComponent,
            bitsPerPixel: image.bitsPerPixel,
            bytesPerRow: image.bytesPerRow,
            space: colorSpace,
            bitmapInfo: image.bitmapInfo,
            provider: independentProvider,
            decode: image.decode,
            shouldInterpolate: image.shouldInterpolate,
            intent: image.renderingIntent
        )
    }

    private static func makeError(_ message: String) -> NSError {
        NSError(
            domain: "com.shennagyp.amscope.capture",
            code: 1,
            userInfo: [NSLocalizedDescriptionKey: message]
        )
    }
}
