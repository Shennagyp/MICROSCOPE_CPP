import Foundation

/// Stable identity for a tool panel. Titles and array positions are presentation
/// data and must never be used as panel identity.
struct NativeDockPanelID: RawRepresentable, Hashable, Codable, ExpressibleByStringLiteral, CustomStringConvertible {
    let rawValue: String

    init(rawValue: String) {
        self.rawValue = rawValue
    }

    init(stringLiteral value: String) {
        self.init(rawValue: value)
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        rawValue = try container.decode(String.self)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(rawValue)
    }

    var description: String { rawValue }

    static let cameraControls: NativeDockPanelID = "cameraControls"
    static let smartGallery: NativeDockPanelID = "smartGallery"
}

/// Edge of the main workspace to which a dock region or auto-hide group belongs.
enum NativeDockEdge: String, Codable, CaseIterable, Hashable {
    case leading
    case trailing
    case top
    case bottom
}

/// Orientation of a split between two dock regions.
enum NativeDockAxis: String, Codable, Equatable {
    case horizontal
    case vertical
}

/// A tab group owns the ordered panel IDs and the selected panel ID.
/// Panel content and view-controller instances live in the runtime registry.
struct NativeDockTabGroup: Codable, Equatable, Identifiable {
    let id: UUID
    var panelIDs: [NativeDockPanelID]
    var activePanelID: NativeDockPanelID

    init(
        id: UUID = UUID(),
        panelIDs: [NativeDockPanelID],
        activePanelID: NativeDockPanelID
    ) {
        self.id = id
        self.panelIDs = panelIDs
        self.activePanelID = activePanelID
    }
}

/// Recursive layout for one dock edge or one floating tool window.
/// A split contains two regions; each leaf is a tab group.
indirect enum NativeDockRegionNode: Codable, Equatable {
    case tabGroup(NativeDockTabGroup)
    case split(NativeDockSplit)

    private enum CodingKeys: String, CodingKey {
        case type
        case tabGroup
        case split
    }

    private enum NodeType: String, Codable {
        case tabGroup
        case split
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        switch try container.decode(NodeType.self, forKey: .type) {
        case .tabGroup:
            self = .tabGroup(try container.decode(
                NativeDockTabGroup.self,
                forKey: .tabGroup
            ))
        case .split:
            self = .split(try container.decode(
                NativeDockSplit.self,
                forKey: .split
            ))
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .tabGroup(let tabGroup):
            try container.encode(NodeType.tabGroup, forKey: .type)
            try container.encode(tabGroup, forKey: .tabGroup)
        case .split(let split):
            try container.encode(NodeType.split, forKey: .type)
            try container.encode(split, forKey: .split)
        }
    }
}

struct NativeDockSplit: Codable, Equatable, Identifiable {
    let id: UUID
    var axis: NativeDockAxis

    /// Fraction of the available extent assigned to the first child.
    /// Valid persisted values are strictly between zero and one.
    var firstProportion: Double

    var first: NativeDockRegionNode
    var second: NativeDockRegionNode

    init(
        id: UUID = UUID(),
        axis: NativeDockAxis,
        firstProportion: Double,
        first: NativeDockRegionNode,
        second: NativeDockRegionNode
    ) {
        self.id = id
        self.axis = axis
        self.firstProportion = firstProportion
        self.first = first
        self.second = second
    }
}

struct NativeDockEdgeRegion: Codable, Equatable, Identifiable {
    var id: String { edge.rawValue }

    let edge: NativeDockEdge
    var root: NativeDockRegionNode

    init(edge: NativeDockEdge, root: NativeDockRegionNode) {
        self.edge = edge
        self.root = root
    }
}

struct NativeDockAutoHideGroup: Codable, Equatable, Identifiable {
    var id: UUID { group.id }

    let edge: NativeDockEdge
    var group: NativeDockTabGroup

    init(edge: NativeDockEdge, group: NativeDockTabGroup) {
        self.edge = edge
        self.group = group
    }
}

/// A window frame in AppKit's screen coordinate space.
struct NativeDockWindowFrame: Codable, Equatable {
    var x: Double
    var y: Double
    var width: Double
    var height: Double

    static let minimumWidth = 280.0
    static let minimumHeight = 320.0

    var isValid: Bool {
        x.isFinite &&
        y.isFinite &&
        width.isFinite &&
        height.isFinite &&
        width >= Self.minimumWidth &&
        height >= Self.minimumHeight
    }
}

struct NativeDockFloatingWindowState: Codable, Equatable, Identifiable {
    let id: UUID
    var root: NativeDockRegionNode
    var frame: NativeDockWindowFrame
    var screenIdentifier: String?
    var isFullScreen: Bool

    init(
        id: UUID = UUID(),
        root: NativeDockRegionNode,
        frame: NativeDockWindowFrame,
        screenIdentifier: String? = nil,
        isFullScreen: Bool = false
    ) {
        self.id = id
        self.root = root
        self.frame = frame
        self.screenIdentifier = screenIdentifier
        self.isFullScreen = isFullScreen
    }
}

/// Complete serializable placement snapshot. The central microscope preview
/// is deliberately not part of this tree: it remains mounted by the main
/// window and is not a detachable tool panel.
struct NativeDockLayoutSnapshot: Codable, Equatable {
    static let currentVersion = 1

    var version: Int
    var dockedRegions: [NativeDockEdgeRegion]
    var autoHiddenGroups: [NativeDockAutoHideGroup]
    var floatingWindows: [NativeDockFloatingWindowState]

    init(
        version: Int = NativeDockLayoutSnapshot.currentVersion,
        dockedRegions: [NativeDockEdgeRegion] = [],
        autoHiddenGroups: [NativeDockAutoHideGroup] = [],
        floatingWindows: [NativeDockFloatingWindowState] = []
    ) {
        self.version = version
        self.dockedRegions = dockedRegions
        self.autoHiddenGroups = autoHiddenGroups
        self.floatingWindows = floatingWindows
    }

    /// Initial layout matching the current application: one trailing region
    /// with Camera Controls and Smart Gallery in the same tab group.
    static var defaultLayout: NativeDockLayoutSnapshot {
        let initialGroup = NativeDockTabGroup(
            id: UUID(uuidString: "A1A1A1A1-1000-4000-8000-000000000801")!,
            panelIDs: [.cameraControls, .smartGallery],
            activePanelID: .cameraControls
        )
        return NativeDockLayoutSnapshot(
            dockedRegions: [
                NativeDockEdgeRegion(
                    edge: .trailing,
                    root: .tabGroup(initialGroup)
                )
            ]
        )
    }

    func allPanelIDs() -> [NativeDockPanelID] {
        var result: [NativeDockPanelID] = []

        for region in dockedRegions {
            region.root.appendPanelIDs(to: &result)
        }
        for autoHidden in autoHiddenGroups {
            result.append(contentsOf: autoHidden.group.panelIDs)
        }
        for floatingWindow in floatingWindows {
            floatingWindow.root.appendPanelIDs(to: &result)
        }

        return result
    }

    func placement(of panelID: NativeDockPanelID) -> NativeDockPanelPlacement {
        for region in dockedRegions {
            if let groupID = region.root.groupID(containing: panelID) {
                return .docked(edge: region.edge, groupID: groupID)
            }
        }

        for autoHidden in autoHiddenGroups where autoHidden.group.panelIDs.contains(panelID) {
            return .autoHidden(edge: autoHidden.edge, groupID: autoHidden.group.id)
        }

        for floatingWindow in floatingWindows {
            if let groupID = floatingWindow.root.groupID(containing: panelID) {
                return .floating(windowID: floatingWindow.id, groupID: groupID)
            }
        }

        return .unplaced
    }
}

enum NativeDockPanelPlacement: Equatable {
    case docked(edge: NativeDockEdge, groupID: UUID)
    case autoHidden(edge: NativeDockEdge, groupID: UUID)
    case floating(windowID: UUID, groupID: UUID)
    case unplaced
}

private extension NativeDockRegionNode {
    func appendPanelIDs(to result: inout [NativeDockPanelID]) {
        switch self {
        case .tabGroup(let group):
            result.append(contentsOf: group.panelIDs)
        case .split(let split):
            split.first.appendPanelIDs(to: &result)
            split.second.appendPanelIDs(to: &result)
        }
    }

    func groupID(containing panelID: NativeDockPanelID) -> UUID? {
        switch self {
        case .tabGroup(let group):
            return group.panelIDs.contains(panelID) ? group.id : nil
        case .split(let split):
            return split.first.groupID(containing: panelID)
                ?? split.second.groupID(containing: panelID)
        }
    }
}

struct NativeDockLayoutIssue: Equatable, CustomStringConvertible {
    let path: String
    let message: String

    var description: String {
        "\(path): \(message)"
    }
}

/// Structural validation is kept separate from rendering so a saved layout
/// can be rejected before it creates windows or reparents any view controller.
enum NativeDockLayoutValidator {
    static func validate(
        _ snapshot: NativeDockLayoutSnapshot
    ) -> [NativeDockLayoutIssue] {
        var issues: [NativeDockLayoutIssue] = []

        if snapshot.version != NativeDockLayoutSnapshot.currentVersion {
            issues.append(NativeDockLayoutIssue(
                path: "version",
                message: "Unsupported layout version \(snapshot.version)."
            ))
        }

        var seenEdges = Set<NativeDockEdge>()
        var seenPanelIDs = Set<NativeDockPanelID>()
        var seenGroupIDs = Set<UUID>()
        var seenSplitIDs = Set<UUID>()
        var seenWindowIDs = Set<UUID>()

        for (index, region) in snapshot.dockedRegions.enumerated() {
            if !seenEdges.insert(region.edge).inserted {
                issues.append(NativeDockLayoutIssue(
                    path: "dockedRegions[\(index)].edge",
                    message: "The \(region.edge.rawValue) edge has more than one root region."
                ))
            }

            validate(
                region.root,
                path: "dockedRegions[\(index)].root",
                seenPanelIDs: &seenPanelIDs,
                seenGroupIDs: &seenGroupIDs,
                seenSplitIDs: &seenSplitIDs,
                issues: &issues
            )
        }

        for (index, autoHidden) in snapshot.autoHiddenGroups.enumerated() {
            validate(
                autoHidden.group,
                path: "autoHiddenGroups[\(index)].group",
                seenPanelIDs: &seenPanelIDs,
                seenGroupIDs: &seenGroupIDs,
                issues: &issues
            )
        }

        for (index, floatingWindow) in snapshot.floatingWindows.enumerated() {
            if !seenWindowIDs.insert(floatingWindow.id).inserted {
                issues.append(NativeDockLayoutIssue(
                    path: "floatingWindows[\(index)].id",
                    message: "Floating-window IDs must be unique."
                ))
            }

            if !floatingWindow.frame.isValid {
                issues.append(NativeDockLayoutIssue(
                    path: "floatingWindows[\(index)].frame",
                    message: "Frame must be finite and at least \(NativeDockWindowFrame.minimumWidth) × \(NativeDockWindowFrame.minimumHeight) points."
                ))
            }

            validate(
                floatingWindow.root,
                path: "floatingWindows[\(index)].root",
                seenPanelIDs: &seenPanelIDs,
                seenGroupIDs: &seenGroupIDs,
                seenSplitIDs: &seenSplitIDs,
                issues: &issues
            )
        }

        return issues
    }

    private static func validate(
        _ node: NativeDockRegionNode,
        path: String,
        seenPanelIDs: inout Set<NativeDockPanelID>,
        seenGroupIDs: inout Set<UUID>,
        seenSplitIDs: inout Set<UUID>,
        issues: inout [NativeDockLayoutIssue]
    ) {
        switch node {
        case .tabGroup(let group):
            validate(
                group,
                path: path,
                seenPanelIDs: &seenPanelIDs,
                seenGroupIDs: &seenGroupIDs,
                issues: &issues
            )

        case .split(let split):
            if !seenSplitIDs.insert(split.id).inserted {
                issues.append(NativeDockLayoutIssue(
                    path: "\(path).id",
                    message: "Split IDs must be unique."
                ))
            }

            if !split.firstProportion.isFinite ||
               split.firstProportion <= 0 ||
               split.firstProportion >= 1 {
                issues.append(NativeDockLayoutIssue(
                    path: "\(path).firstProportion",
                    message: "Split proportion must be finite and strictly between zero and one."
                ))
            }

            validate(
                split.first,
                path: "\(path).first",
                seenPanelIDs: &seenPanelIDs,
                seenGroupIDs: &seenGroupIDs,
                seenSplitIDs: &seenSplitIDs,
                issues: &issues
            )
            validate(
                split.second,
                path: "\(path).second",
                seenPanelIDs: &seenPanelIDs,
                seenGroupIDs: &seenGroupIDs,
                seenSplitIDs: &seenSplitIDs,
                issues: &issues
            )
        }
    }

    private static func validate(
        _ group: NativeDockTabGroup,
        path: String,
        seenPanelIDs: inout Set<NativeDockPanelID>,
        seenGroupIDs: inout Set<UUID>,
        issues: inout [NativeDockLayoutIssue]
    ) {
        if !seenGroupIDs.insert(group.id).inserted {
            issues.append(NativeDockLayoutIssue(
                path: "\(path).id",
                message: "Tab-group IDs must be unique across the complete layout."
            ))
        }

        if group.panelIDs.isEmpty {
            issues.append(NativeDockLayoutIssue(
                path: "\(path).panelIDs",
                message: "A tab group must contain at least one panel."
            ))
            return
        }

        if !group.panelIDs.contains(group.activePanelID) {
            issues.append(NativeDockLayoutIssue(
                path: "\(path).activePanelID",
                message: "The active panel must be a member of this tab group."
            ))
        }

        var localPanelIDs = Set<NativeDockPanelID>()
        for (index, panelID) in group.panelIDs.enumerated() {
            if panelID.rawValue.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                issues.append(NativeDockLayoutIssue(
                    path: "\(path).panelIDs[\(index)]",
                    message: "Panel IDs cannot be empty."
                ))
            }

            if !localPanelIDs.insert(panelID).inserted {
                issues.append(NativeDockLayoutIssue(
                    path: "\(path).panelIDs[\(index)]",
                    message: "A panel cannot appear twice in one tab group."
                ))
            }

            if !seenPanelIDs.insert(panelID).inserted {
                issues.append(NativeDockLayoutIssue(
                    path: "\(path).panelIDs[\(index)]",
                    message: "A panel can occupy only one location in the complete layout."
                ))
            }
        }
    }
}
