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
    let thumbnailData: Data

    var dimensionsText: String {
        "\(pixelWidth) × \(pixelHeight)"
    }
}

@MainActor
final class NativeCaptureGalleryStore: ObservableObject {
    @Published private(set) var captures: [NativeCaptureItem] = []
    @Published private(set) var isCapturing = false
    @Published private(set) var captureErrorMessage: String?

    private let maximumVisibleCaptureCount = 80
    private let maximumConcurrentCaptures = 1
    private var pendingCaptureCount = 0

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

    init() {
        loadExistingCaptures()
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
        let captureSource = NativeCaptureImageSource(image: image)

        // Only the explicit user capture references this source image. The
        // worker detaches its pixels, clears the source holder, and then does
        // PNG compression and thumbnail generation without retaining the
        // camera frame-pool lease as gallery history.
        DispatchQueue.global(qos: .userInitiated).async { [weak self, captureSource] in
            let result = NativeCaptureFileCodec.writeCapture(
                from: captureSource,
                to: outputURL,
                slide: slide
            )
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.pendingCaptureCount = max(0, self.pendingCaptureCount - 1)
                self.isCapturing = self.pendingCaptureCount > 0

                switch result {
                case .success(let item):
                    self.captures.insert(item, at: 0)
                    self.trimGallery()
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

    func dismissCaptureError() {
        captureErrorMessage = nil
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
                let pngURLs = urls
                    .filter { $0.pathExtension.lowercased() == "png" }
                    .sorted {
                        let left = (try? $0.resourceValues(forKeys: [.creationDateKey, .contentModificationDateKey]))
                        let right = (try? $1.resourceValues(forKeys: [.creationDateKey, .contentModificationDateKey]))
                        let leftDate = left?.creationDate ?? left?.contentModificationDate ?? .distantPast
                        let rightDate = right?.creationDate ?? right?.contentModificationDate ?? .distantPast
                        return leftDate > rightDate
                    }

                // Keep all capture files on disk. Only the most recent subset
                // is loaded into the in-memory gallery thumbnail list.
                loadedItems = pngURLs.prefix(maximumCount).compactMap {
                    NativeCaptureFileCodec.loadCapture(at: $0)
                }
            } catch {
                loadedItems = []
            }

            DispatchQueue.main.async {
                guard let self = self else { return }
                let currentIDs = Set(self.captures.map(\.id))
                let merged = self.captures + loadedItems.filter { !currentIDs.contains($0.id) }
                self.captures = merged.sorted { $0.capturedAt > $1.capturedAt }
                self.trimGallery()
            }
        }
    }
}

private final class NativeCaptureImageSource {
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
        slide: NativeSlideID
    ) -> Result<NativeCaptureItem, NSError> {
        var liveImage = source.takeImage()
        guard let independentImage = copyPixelsAndRelease(&liveImage) else {
            return .failure(makeError("Could not detach the image from the live camera frame."))
        }

        return writeIndependentCapture(
            independentImage,
            to: url,
            slide: slide
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
        slide: NativeSlideID
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

        guard let item = loadCapture(at: url, slideOverride: slide) else {
            try? FileManager.default.removeItem(at: url)
            return .failure(makeError("The capture was saved but its thumbnail could not be generated."))
        }
        return .success(item)
    }

    static func loadCapture(
        at url: URL,
        slideOverride: NativeSlideID? = nil
    ) -> NativeCaptureItem? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let properties = CGImageSourceCopyPropertiesAtIndex(
                source,
                0,
                nil
              ) as? [CFString: Any],
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

        let width = properties[kCGImagePropertyPixelWidth] as? Int ?? thumbnail.width
        let height = properties[kCGImagePropertyPixelHeight] as? Int ?? thumbnail.height
        let slide = slideOverride ?? (
            url.lastPathComponent.hasSuffix("-slide2.png") ? .slide2 : .slide1
        )
        let values = try? url.resourceValues(forKeys: [.creationDateKey, .contentModificationDateKey])
        let capturedAt = values?.creationDate ?? values?.contentModificationDate ?? Date()

        let thumbnailData = NSMutableData()
        guard let thumbnailDestination = CGImageDestinationCreateWithData(
            thumbnailData,
            "public.png" as CFString,
            1,
            nil
        ) else {
            return nil
        }
        CGImageDestinationAddImage(thumbnailDestination, thumbnail, nil)
        guard CGImageDestinationFinalize(thumbnailDestination) else {
            return nil
        }

        return NativeCaptureItem(
            id: url.lastPathComponent,
            fileURL: url,
            slide: slide,
            capturedAt: capturedAt,
            pixelWidth: width,
            pixelHeight: height,
            thumbnailData: thumbnailData as Data
        )
    }

    private static func copyPixels(of image: CGImage) -> CGImage? {
        guard let sourceProvider = image.dataProvider,
              let copiedData = CGDataProviderCopyData(sourceProvider),
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
