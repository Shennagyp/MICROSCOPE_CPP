import SwiftUI

struct MainWindowView: View {
    @State private var exposureValue = "Auto"
    @State private var gainValue = "—"
    @State private var pixelFormatValue = "—"
    @State private var resolutionValue = "—"

    var body: some View {
        VStack(spacing: 0) {
            NativePreviewSurface()
                .frame(maxWidth: .infinity, maxHeight: .infinity)

            Divider()

            HStack(spacing: 22) {
                InformationBarField(
                    title: "Camera",
                    value: .constant("Disconnected"),
                    isEditable: false
                )
                InformationBarField(
                    title: "Resolution",
                    value: $resolutionValue,
                    isEditable: false
                )
                InformationBarField(
                    title: "Pixel Format",
                    value: $pixelFormatValue,
                    isEditable: false
                )
                InformationBarField(
                    title: "Exposure",
                    value: $exposureValue,
                    isEditable: true
                )
                InformationBarField(
                    title: "Gain",
                    value: $gainValue,
                    isEditable: true
                )

                Spacer(minLength: 8)

                Text("Not streaming")
                    .foregroundStyle(.secondary)
            }
            .font(.system(size: 12))
            .padding(.horizontal, 14)
            .padding(.vertical, 9)
            .background(.bar)
        }
        .frame(minWidth: 960, minHeight: 640)
    }
}

private struct InformationBarField: View {
    let title: String
    @Binding var value: String
    let isEditable: Bool

    @State private var draft = ""
    @State private var isEditing = false

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title)
                .font(.system(size: 10, weight: .medium))
                .foregroundStyle(.secondary)

            Text(value)
                .font(.system(size: 12, weight: .medium, design: .monospaced))
                .foregroundStyle(isEditable ? Color.primary : Color.secondary)
                .contentShape(Rectangle())
                .onTapGesture(count: 2) {
                    guard isEditable else { return }
                    draft = value
                    isEditing = true
                }
                .popover(isPresented: $isEditing) {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Edit \(title)")
                            .font(.headline)

                        TextField(title, text: $draft)
                            .textFieldStyle(.roundedBorder)

                        HStack {
                            Spacer()
                            Button("Cancel") {
                                isEditing = false
                            }
                            Button("Apply") {
                                value = draft
                                isEditing = false
                            }
                            .keyboardShortcut(.defaultAction)
                        }
                    }
                    .padding(14)
                    .frame(width: 260)
                }
                .help(isEditable ? "Double-click to edit" : title)
        }
        .fixedSize(horizontal: true, vertical: false)
    }
}
