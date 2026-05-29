import SwiftUI
import QuickLook

struct ArchiveBrowserView: View {
    @EnvironmentObject var service: ArchiveService
    @State private var searchText = ""
    @State private var showExtract = false
    @State private var showExtractHereConfirm = false
    @State private var previewURL: URL?
    @State private var infoNode: TreeNode?
    @State private var showInfo = false

    private var filteredEntries: [ArchiveEntry] {
        if searchText.isEmpty { return service.entries }
        return service.entries.filter {
            $0.name.localizedCaseInsensitiveContains(searchText)
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            table
            if case .extracting(let progress) = service.action {
                ProgressBar(value: progress, label: "Extracting...")
            }
            if case .creating(let progress) = service.action {
                ProgressBar(value: progress, label: "Creating...")
            }
        }
        .sheet(isPresented: $showExtract) {
            ExtractSetupView()
        }
        .alert("Extract Here?", isPresented: $showExtractHereConfirm) {
            Button("Cancel", role: .cancel) { }
            Button("Extract") {
                let parent = (service.archivePath as NSString).deletingLastPathComponent
                let dest = parent + "/" + service.archiveName.fileNameWithoutExtension
                Task { try? await service.extractArchive(to: dest) }
            }
        } message: {
            Text("Extract all files to:\n\((service.archivePath as NSString).deletingLastPathComponent)/\(service.archiveName.fileNameWithoutExtension)")
        }
        .alert(infoNode?.name ?? "", isPresented: $showInfo, actions: {
            Button("OK") { }
        }, message: {
            if let node = infoNode {
                Text("""
                Size: \(Formatting.shortSize(node.size))
                Compressed: \(Formatting.shortSize(node.compressedSize))
                Date: \(Formatting.date(node.date))
                Type: \(node.isFolder ? "Folder" : "File")
                """)
            }
        })
        .quickLookPreview($previewURL)
    }

    // MARK: - Header

    private var header: some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 2) {
                Text(service.archiveName)
                    .font(.title3.weight(.semibold))
                    .lineLimit(1)
                infoText
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            HStack(spacing: 6) {
                extractMenu
                Button { service.closeArchive() } label: {
                    Image(systemName: "xmark.circle.fill")
                        .font(.title3)
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
    }

    @ViewBuilder
    private var infoText: some View {
        if let ratio = service.compressionRatio() {
            Text("\(service.entries.count) items  ·  \(Formatting.shortSize(service.totalSize()))  ·  \(Int(ratio * 100))% compression")
        } else {
            Text("\(service.entries.count) items  ·  \(Formatting.shortSize(service.totalSize()))")
        }
    }

    // MARK: - Extract Menu

    private var extractMenu: some View {
        Menu {
            Button { showExtract = true } label: {
                Label("Extract to Folder...", systemImage: "arrow.down.to.line")
            }
            Button { showExtractHereConfirm = true } label: {
                Label("Extract Here", systemImage: "arrow.down.doc")
            }
        } label: {
            Label("Extract", systemImage: "arrow.down.to.line.compact")
                .font(.subheadline)
        }
        .menuStyle(.button)
        .buttonStyle(.bordered)
        .controlSize(.small)
    }

    // MARK: - Table / Tree

    private var table: some View {
        Group {
            if searchText.isEmpty {
                treeView
            } else {
                searchResults
            }
        }
        .searchable(text: $searchText, placement: .automatic, prompt: "Search files...")
    }

    private var treeView: some View {
        List {
            ForEach(service.tree) { node in
                TreeNodeRow(node: node, preview: { previewNode($0) },
                            showInfo: { infoNode = $0; showInfo = true },
                            copyName: { copyName($0) })
            }
        }
        .listStyle(.sidebar)
    }

    private var searchResults: some View {
        Group {
            if filteredEntries.isEmpty {
                emptyState
            } else {
                List(filteredEntries) { entry in
                    FileRowView(entry: entry)
                        .onTapGesture(count: 2) {
                            preview(entry)
                        }
                        .contextMenu {
                            if !entry.isFolder {
                                Button { preview(entry) } label: {
                                    Label("Open", systemImage: "eye")
                                }
                            }
                            Button { copyName(entry.name) } label: {
                                Label("Copy Name", systemImage: "doc.on.doc")
                            }
                            Divider()
                        }
                }
                .listStyle(.sidebar)
            }
        }
    }

    private var emptyState: some View {
        VStack(spacing: 8) {
            Image(systemName: searchText.isEmpty ? "tray" : "magnifyingglass")
                .font(.title)
                .foregroundStyle(.tertiary)
            Text(searchText.isEmpty ? "Archive is empty" : "No results for \"\(searchText)\"")
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Actions

    private func previewNode(_ node: TreeNode) {
        guard !node.isFolder, let entry = node.entry else { return }
        preview(entry)
    }

    private func preview(_ entry: ArchiveEntry) {
        guard !entry.isFolder else { return }
        let tempDir = FileManager.default.temporaryDirectory
            .appendingPathComponent("7zip_preview_\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: tempDir, withIntermediateDirectories: true)

        Task {
            let dest = tempDir.path
            try? await service.extractArchive(to: dest)
            let filePath = tempDir.appendingPathComponent(entry.path)
            if FileManager.default.fileExists(atPath: filePath.path) {
                previewURL = filePath
            }
        }
    }

    private func copyName(_ name: String) {
        #if os(macOS)
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(name, forType: .string)
        #else
        UIPasteboard.general.string = name
        #endif
    }
}

// MARK: - Tree Row

struct TreeNodeRow: View {
    let node: TreeNode
    let preview: (TreeNode) -> Void
    let showInfo: (TreeNode) -> Void
    let copyName: (String) -> Void

    var body: some View {
        if node.isFolder, !node.children.isEmpty {
            DisclosureGroup(
                content: {
                    ForEach(node.children) { child in
                        TreeNodeRow(node: child, preview: preview,
                                    showInfo: showInfo, copyName: copyName)
                            .padding(.leading, 16)
                    }
                },
                label: {
                    FileRowView(node: node)
                }
            )
        } else {
            FileRowView(node: node)
                .onTapGesture(count: 2) { preview(node) }
                .contextMenu {
                    if !node.isFolder {
                        Button { preview(node) } label: {
                            Label("Open", systemImage: "eye")
                        }
                    }
                    Button { copyName(node.name) } label: {
                        Label("Copy Name", systemImage: "doc.on.doc")
                    }
                    Divider()
                    Button {
                        showInfo(node)
                    } label: {
                        Label("Properties", systemImage: "info.circle")
                    }
                }
        }
    }
}

// MARK: - Progress Bar

struct ProgressBar: View {
    let value: Double
    let label: String

    var body: some View {
        VStack(spacing: 4) {
            HStack {
                Text(label)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Text("\(Int(value * 100))%")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
            ProgressView(value: value)
                .tint(.accentColor)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
        .background(.ultraThinMaterial)
        .transition(.move(edge: .bottom).combined(with: .opacity))
    }
}
