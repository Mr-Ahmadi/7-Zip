import SwiftUI

struct ExtractSetupView: View {
    @EnvironmentObject var service: ArchiveService
    @Environment(\.dismiss) private var dismiss

    @State private var destination: String
    @State private var usePassword = false
    @State private var password = ""

    private var fileName: String { service.archiveName }
    private var fileSize: Int64 {
        (try? FileManager.default.attributesOfItem(atPath: service.archivePath)[.size] as? Int64) ?? 0
    }

    init() {
        let basePath = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Desktop").path
        let name = "Extracted"
        _destination = State(initialValue: "\(basePath)/\(name)")
    }

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                ScrollView {
                    VStack(alignment: .leading, spacing: 20) {
                        archiveInfoSection
                        destinationSection
                        passwordSection
                    }
                    .padding(20)
                }

                Divider()
                footerView
            }
            .frame(idealWidth: 420, idealHeight: 360)
            .navigationTitle("Extract Archive")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
            }
        }
    }

    private var archiveInfoSection: some View {
        HStack(spacing: 12) {
            Image(systemName: "archivebox.fill")
                .font(.title2)
                .foregroundStyle(.tint)

            VStack(alignment: .leading, spacing: 2) {
                Text(fileName)
                    .font(.headline)
                Text("\(Formatting.shortSize(fileSize))  ·  \(service.entries.count) items")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer()
        }
        .padding(12)
        .background(.quaternary.opacity(0.15), in: RoundedRectangle(cornerRadius: 10))
    }

    private var destinationSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            sectionHeader("DESTINATION")
            Text("Files will be extracted to this folder")
                .font(.caption)
                .foregroundStyle(.tertiary)

            TextField("Destination", text: $destination)
                .textFieldStyle(.roundedBorder)
        }
    }

    private var passwordSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            sectionHeader("PASSWORD (OPTIONAL)")

            Toggle("Archive is encrypted", isOn: $usePassword)
                .toggleStyle(.switch)
                .controlSize(.small)

            if usePassword {
                SecureField("Enter password", text: $password)
                    .textFieldStyle(.roundedBorder)
            }
        }
    }

    private var footerView: some View {
        HStack {
            Spacer()
            Button("Cancel", role: .cancel) { dismiss() }
                .buttonStyle(.bordered)
                .keyboardShortcut(.escape)
            Button("Extract") { extract() }
                .buttonStyle(.borderedProminent)
                .disabled(destination.trimmingCharacters(in: .whitespaces).isEmpty)
                .keyboardShortcut(.return)
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 12)
        .background(.regularMaterial)
    }

    private func extract() {
        Task {
            try? await service.extractArchive(
                to: destination,
                password: usePassword ? password : ""
            )
            dismiss()
        }
    }

    private func sectionHeader(_ text: String) -> some View {
        Text(text)
            .font(.caption.weight(.semibold))
            .foregroundStyle(.tertiary)
            .tracking(1)
    }
}
