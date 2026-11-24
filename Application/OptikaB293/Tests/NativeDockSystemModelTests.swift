import Foundation

@main
struct NativeDockSystemModelTests {
    static func main() throws {
        testDefaultLayoutAndPlacement()
        try testCodableRoundTrip()
        testDuplicatePanelPlacementIsRejected()
        testInvalidActiveTabIsRejected()
        testInvalidSplitProportionIsRejected()
        testInvalidFloatingFrameIsRejected()

        print("Native dock layout model tests passed.")
    }

    private static func testDefaultLayoutAndPlacement() {
        let layout = NativeDockLayoutSnapshot.defaultLayout
        let issues = NativeDockLayoutValidator.validate(layout)

        precondition(issues.isEmpty, "Default layout must be valid: \(issues)")
        precondition(
            layout.placement(of: .cameraControls) ==
                .docked(
                    edge: .trailing,
                    groupID: UUID(uuidString: "A1A1A1A1-1000-4000-8000-000000000801")!
                ),
            "Camera Controls must be in the default trailing tab group."
        )
        precondition(
            layout.placement(of: .smartGallery) ==
                .docked(
                    edge: .trailing,
                    groupID: UUID(uuidString: "A1A1A1A1-1000-4000-8000-000000000801")!
                ),
            "Smart Gallery must share the default trailing tab group."
        )
        precondition(
            layout.placement(of: "futurePanel") == .unplaced,
            "An unregistered panel must resolve as unplaced."
        )
    }

    private static func testCodableRoundTrip() throws {
        let original = NativeDockLayoutSnapshot.defaultLayout
        let encoded = try JSONEncoder().encode(original)
        let decoded = try JSONDecoder().decode(
            NativeDockLayoutSnapshot.self,
            from: encoded
        )

        precondition(decoded == original, "Dock layout Codable round-trip changed the model.")
        precondition(
            NativeDockLayoutValidator.validate(decoded).isEmpty,
            "Decoded default layout must remain valid."
        )
    }

    private static func testDuplicatePanelPlacementIsRejected() {
        var layout = NativeDockLayoutSnapshot.defaultLayout
        let duplicate = NativeDockTabGroup(
            panelIDs: [.smartGallery],
            activePanelID: .smartGallery
        )
        layout.autoHiddenGroups.append(
            NativeDockAutoHideGroup(edge: .trailing, group: duplicate)
        )

        let issues = NativeDockLayoutValidator.validate(layout)
        precondition(
            issues.contains(where: { $0.message.contains("only one location") }),
            "A panel duplicated across docked and auto-hidden locations must be rejected."
        )
    }

    private static func testInvalidActiveTabIsRejected() {
        var layout = NativeDockLayoutSnapshot.defaultLayout
        var group = NativeDockTabGroup(
            panelIDs: [.cameraControls],
            activePanelID: .cameraControls
        )
        group.activePanelID = .smartGallery
        layout.dockedRegions = [
            NativeDockEdgeRegion(edge: .trailing, root: .tabGroup(group))
        ]

        let issues = NativeDockLayoutValidator.validate(layout)
        precondition(
            issues.contains(where: { $0.message.contains("active panel must be a member") }),
            "An active panel outside its tab group must be rejected."
        )
    }

    private static func testInvalidSplitProportionIsRejected() {
        let firstGroup = NativeDockTabGroup(
            panelIDs: [.cameraControls],
            activePanelID: .cameraControls
        )
        let secondGroup = NativeDockTabGroup(
            panelIDs: [.smartGallery],
            activePanelID: .smartGallery
        )
        let split = NativeDockSplit(
            axis: .horizontal,
            firstProportion: .infinity,
            first: .tabGroup(firstGroup),
            second: .tabGroup(secondGroup)
        )
        let layout = NativeDockLayoutSnapshot(
            dockedRegions: [
                NativeDockEdgeRegion(edge: .trailing, root: .split(split))
            ]
        )

        let issues = NativeDockLayoutValidator.validate(layout)
        precondition(
            issues.contains(where: { $0.message.contains("Split proportion") }),
            "A non-finite split proportion must be rejected."
        )
    }

    private static func testInvalidFloatingFrameIsRejected() {
        let group = NativeDockTabGroup(
            panelIDs: [.cameraControls],
            activePanelID: .cameraControls
        )
        let floatingWindow = NativeDockFloatingWindowState(
            root: .tabGroup(group),
            frame: NativeDockWindowFrame(
                x: .nan,
                y: 100,
                width: 240,
                height: 300
            )
        )
        let layout = NativeDockLayoutSnapshot(
            floatingWindows: [floatingWindow]
        )

        let issues = NativeDockLayoutValidator.validate(layout)
        precondition(
            issues.contains(where: { $0.path.hasSuffix(".frame") }),
            "A non-finite or undersized floating frame must be rejected."
        )
    }
}
