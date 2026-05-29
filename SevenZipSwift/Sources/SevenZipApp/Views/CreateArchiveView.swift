import SwiftUI

struct CreateArchiveView: View {
    @EnvironmentObject var service: ArchiveService
    @Environment(\.dismiss) private var dismiss

    @State private var items: [SelectedItem] = []
    @State private var archiveName = ""
    @State private var selectedFormat: ArchiveFormat = .zip
    @State private var compressionLevel: Int = 5
    @State private var usePassword = false
    @State private var password = ""
    @State private var confirmPassword = ""
    @State private var isCreating = false

    private var canCreate: Bool {
        !items.isEmpty && !archiveName.trimmingCharacters(in: .whitespaces).isEmpty
        && (!usePassword || (!password.isEmpty && password == confirmPassword))
    }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    filesSection
                    optionsSection
                    passwordSection
                }
                .padding(20)
            }
            .safeAreaInset(edge: .bottom) { footerView }
            .navigationTitle("Create Archive")
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .disabled(isCreating)
            .overlay { if isCreating { creatingOverlay } }
        }
    }

    // MARK: - Model

    struct SelectedItem: Identifiable, Hashable {
        let id = UUID()
        let path: String
        let isDirectory: Bool
        let fileName: String
    }

    // MARK: - File pickers

    private func pickFiles() {
        #if os(macOS)
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = true
        panel.title = "Select Files to Archive"
        guard panel.runModal() == .OK else { return }
        addItems(panel.urls)
        #else
        // iOS uses fileImporter — bound to hidden state triggers below
        #endif
    }

    private func pickFolder() {
        #if os(macOS)
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.title = "Select Folder to Archive"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        addItems([url])
        #else
        // iOS uses fileImporter — bound to hidden state triggers below
        #endif
    }

    private func addItems(_ urls: [URL]) {
        for url in urls {
            let path = url.path
            let isDir = (try? url.resourceValues(forKeys: Set([URLResourceKey.isDirectoryKey])).isDirectory) ?? false
            if !items.contains(where: { $0.path == path }) {
                items.append(SelectedItem(path: path, isDirectory: isDir, fileName: url.lastPathComponent))
            }
        }
        if archiveName.trimmingCharacters(in: .whitespaces).isEmpty, let first = items.first {
            archiveName = ((first.path as NSString).deletingPathExtension as NSString).lastPathComponent
        }
    }

    private var archiveFileName: String {
        let name = archiveName.trimmingCharacters(in: .whitespaces)
        if name.isEmpty { return "Archive.\(selectedFormat.rawValue)" }
        if name.hasSuffix(".\(selectedFormat.rawValue)") { return name }
        return "\(name).\(selectedFormat.rawValue)"
    }

    // MARK: - Create

    private func chooseDestination() {
        let fileName = archiveFileName
        #if os(macOS)
        let panel = NSSavePanel()
        panel.nameFieldStringValue = fileName
        panel.title = "Save Archive"
        panel.canCreateDirectories = true
        guard panel.runModal() == .OK, let url = panel.url else { return }
        createArchive(at: url.path)
        #else
        // iOS - write to temp then present share sheet
        let tempURL = FileManager.default.temporaryDirectory.appendingPathComponent(fileName)
        createArchive(at: tempURL.path)
        #endif
    }

    private func createArchive(at path: String) {
        isCreating = true
        Task {
            try? await service.createArchive(
                files: items.map(\.path),
                destination: path,
                format: selectedFormat,
                compressionLevel: compressionLevel,
                password: usePassword ? password : ""
            )
            isCreating = false
            dismiss()
        }
    }

    // MARK: - Overlay

    private var creatingOverlay: some View {
        ZStack {
            Color.black.opacity(0.15).ignoresSafeArea()
            VStack(spacing: 12) {
                ProgressView().scaleEffect(1.2)
                Text("Creating archive...").font(.headline)
            }
            .padding(28)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
        }
    }

    // MARK: - Files Section

    private var filesSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            sectionHeader("FILES")

            GroupBox {
                VStack(spacing: 8) {
                    if items.isEmpty {
                        Text("No files or folders chosen")
                            .foregroundStyle(.tertiary)
                            .frame(maxWidth: .infinity, minHeight: 80)
                    } else {
                        List(items) { item in
                            HStack {
                                Image(systemName: item.isDirectory ? "folder.fill" : "doc.fill")
                                    .foregroundStyle(item.isDirectory ? .blue : .accentColor)
                                Text(item.fileName).lineLimit(1)
                                Spacer()
                                if item.isDirectory {
                                    Text("Folder")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                } else {
                                    let attrs = try? FileManager.default.attributesOfItem(atPath: item.path)
                                    let size = (attrs?[.size] as? Int64) ?? 0
                                    Text(Formatting.shortSize(size))
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            }
                            .padding(.vertical, 2)
                        }
                        .listStyle(.plain)
                        .frame(minHeight: 100, maxHeight: 180)
                    }

                    HStack(spacing: 6) {
                        Button(action: pickFiles) {
                            Label("Add Files", systemImage: "doc.badge.plus")
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)

                        Button(action: pickFolder) {
                            Label("Add Folder", systemImage: "folder.badge.plus")
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)

                        if !items.isEmpty {
                            Button(role: .destructive) { items.removeAll() } label: {
                                Label("Clear All", systemImage: "trash")
                            }
                            .buttonStyle(.bordered)
                            .controlSize(.small)
                        }

                        Spacer()

                        if !items.isEmpty {
                            Text("\(items.count) item(s)")
                                .font(.caption)
                                .foregroundStyle(.tertiary)
                        }
                    }
                }
            }
        }
    }

    // MARK: - Options

    private var optionsSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            sectionHeader("OPTIONS")

            GroupBox {
                VStack(spacing: 12) {
                    HStack {
                        TextField("Archive Name", text: $archiveName)
                            .textFieldStyle(.roundedBorder)
                        Text(".\(selectedFormat.rawValue)")
                            .foregroundStyle(.tertiary)
                            .font(.body.monospaced())
                    }

                    HStack {
                        Text("Format:")
                            .frame(width: 90, alignment: .leading)
                        Picker("Format", selection: $selectedFormat) {
                            ForEach(ArchiveFormat.creatableFormats) { fmt in
                                Text(fmt.displayName).tag(fmt)
                            }
                        }
                        Spacer()
                    }

                    HStack {
                        Text("Compression:")
                            .frame(width: 90, alignment: .leading)
                        Picker("Level", selection: $compressionLevel) {
                            Text("Store (0)").tag(0)
                            Text("Fast (1)").tag(1)
                            Text("Fast (2)").tag(2)
                            Text("Fast (3)").tag(3)
                            Text("Normal (4)").tag(4)
                            Text("Normal (5)").tag(5)
                            Text("Good (6)").tag(6)
                            Text("Good (7)").tag(7)
                            Text("Ultra (8)").tag(8)
                            Text("Ultra (9)").tag(9)
                        }
                        Spacer()
                    }
                }
            }
        }
    }

    // MARK: - Password

    private var passwordSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            sectionHeader("PASSWORD (OPTIONAL)")

            GroupBox {
                VStack(spacing: 10) {
                    Toggle("Protect with password", isOn: $usePassword)
                        .toggleStyle(.switch)
                        .controlSize(.small)

                    if usePassword {
                        SecureField("Password", text: $password)
                            .textFieldStyle(.roundedBorder)
                        SecureField("Confirm Password", text: $confirmPassword)
                            .textFieldStyle(.roundedBorder)
                        if !password.isEmpty && password != confirmPassword {
                            Text("Passwords do not match")
                                .font(.caption)
                                .foregroundStyle(.red)
                        }
                    }
                }
            }
        }
    }

    // MARK: - Footer

    private var footerView: some View {
        HStack {
            Spacer()
            Button("Create Archive") { chooseDestination() }
                .buttonStyle(.borderedProminent)
                .disabled(!canCreate)
                .keyboardShortcut(.return)
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 12)
        .background(.regularMaterial)
    }

    private func sectionHeader(_ text: String) -> some View {
        Text(text)
            .font(.caption.weight(.semibold))
            .foregroundStyle(.tertiary)
            .tracking(1)
    }
}


