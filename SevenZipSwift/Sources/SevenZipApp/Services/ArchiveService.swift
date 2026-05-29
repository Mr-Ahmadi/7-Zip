import Foundation
import CSevenZip

@MainActor
final class ArchiveService: ObservableObject {
    static let shared = ArchiveService()

    @Published var action: ArchiveAction = .idle
    @Published var entries: [ArchiveEntry] = []
    @Published var archivePath: String = ""
    @Published var recentArchives: [String] = []

    /// Set by AppDelegate when opened via "Open With" — consumed by ContentView.
    @Published var pendingPath: String?

    private let archiver: UnsafeMutableRawPointer
    private let recentsKey = "recentArchives"
    private let maxRecents = 8

    private init() {
        archiver = archiver_create()
        loadRecents()
    }

    deinit {
        archiver_destroy(archiver)
    }

    var isArchiveOpen: Bool { !archivePath.isEmpty }
    var archiveName: String { archivePath.fileName }

    /// Builds a hierarchical tree from the flat entries list.
    var tree: [TreeNode] {
        TreeNode.buildTree(from: entries)
    }

    var isToolAvailable: Bool {
        archiver_is_tool_available()
    }

    var toolPath: String {
        guard let cStr = archiver_find_tool() else { return "" }
        let str = String(cString: cStr)
        archiver_free_string(cStr)
        return str
    }

    // MARK: - List Archive

    func openArchive(at path: String) async throws {
        guard archiver_is_archive(path) else {
            throw ArchiveError.unsupportedFormat(path.fileExtension)
        }

        action = .loading
        archivePath = path
        addRecent(path)

        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async { [self] in
                var errorPtr: UnsafeMutablePointer<CChar>?
                let list = archiver_list(archiver, path, &errorPtr)

                if let errorPtr {
                    let msg = String(cString: errorPtr)
                    archiver_free_string(errorPtr)
                    DispatchQueue.main.async {
                        self.action = .failure(msg)
                        continuation.resume(throwing: ArchiveError.listFailed(msg))
                    }
                    return
                }

                let listRef = list!
                var parsed: [ArchiveEntry] = []
                for i in 0..<Int(listRef.pointee.count) {
                    let e = listRef.pointee.entries[i]
                    let date = self.parseDate(String(cString: e.date)) ?? Date()
                    parsed.append(ArchiveEntry(
                        name: String(cString: e.name),
                        path: String(cString: e.path),
                        size: e.size,
                        compressedSize: e.compressedSize,
                        isFolder: e.isFolder,
                        date: date
                    ))
                }
                archiver_free_entries(list)

                DispatchQueue.main.async {
                    self.entries = parsed
                    self.action = .idle
                    continuation.resume()
                }
            }
        }
    }

    func closeArchive() {
        archivePath = ""
        entries = []
        action = .idle
        archiver_cancel(archiver)
    }

    // MARK: - Extract

    func extractArchive(to destination: String, password: String = "") async throws {
        guard !archivePath.isEmpty else { throw ArchiveError.noArchiveOpen }
        action = .extracting(0)

        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async { [self] in
                var errorPtr: UnsafeMutablePointer<CChar>?

                let callback: CProgressCallback = { pct, ctx in
                    let ctxPtr = Unmanaged<ArchiveService>.fromOpaque(ctx!)
                    let service = ctxPtr.takeUnretainedValue()
                    DispatchQueue.main.async {
                        service.action = .extracting(Double(pct) / 100.0)
                    }
                }

                let ctx = Unmanaged.passUnretained(self).toOpaque()

                let ok = archiver_extract(
                    archiver, archivePath, destination,
                    password.isEmpty ? nil : password,
                    &errorPtr, callback, ctx
                )

                DispatchQueue.main.async {
                    if ok {
                        self.action = .success("Extracted to \(destination.fileName)")
                        continuation.resume()
                    } else {
                        let msg = errorPtr.map { String(cString: $0) } ?? "Extraction failed"
                        errorPtr.map { archiver_free_string($0) }
                        self.action = .failure(msg)
                        continuation.resume(throwing: ArchiveError.extractionFailed(msg))
                    }
                }
            }
        }
    }

    // MARK: - Create

    func createArchive(files: [String], destination: String, format: ArchiveFormat,
                       compressionLevel: Int = 5, password: String = "") async throws
    {
        action = .creating(0)

        let cFiles: [UnsafePointer<CChar>?] = files.map { $0.withCString { UnsafePointer(strdup($0)) } }
        defer { cFiles.forEach { if let p = $0 { free(UnsafeMutablePointer(mutating: p)) } } }

        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async { [self] in
                var errorPtr: UnsafeMutablePointer<CChar>?

                let callback: CProgressCallback = { pct, ctx in
                    let ctxPtr = Unmanaged<ArchiveService>.fromOpaque(ctx!)
                    let service = ctxPtr.takeUnretainedValue()
                    DispatchQueue.main.async {
                        service.action = .creating(Double(pct) / 100.0)
                    }
                }

                let ctx = Unmanaged.passUnretained(self).toOpaque()

                let ok = archiver_create_archive(
                    archiver, cFiles, Int32(files.count),
                    destination, format.rawValue, Int32(compressionLevel),
                    password.isEmpty ? nil : password,
                    &errorPtr, callback, ctx
                )

                DispatchQueue.main.async {
                    if ok {
                        self.action = .success("Archive created")
                        continuation.resume()
                    } else {
                        let msg = errorPtr.map { String(cString: $0) } ?? "Creation failed"
                        errorPtr.map { archiver_free_string($0) }
                        self.action = .failure(msg)
                        continuation.resume(throwing: ArchiveError.creationFailed(msg))
                    }
                }
            }
        }
    }

    func cancel() {
        archiver_cancel(archiver)
        action = .idle
    }

    // MARK: - Recents

    func addRecent(_ path: String) {
        recentArchives.removeAll { $0 == path }
        recentArchives.insert(path, at: 0)
        if recentArchives.count > maxRecents {
            recentArchives = Array(recentArchives.prefix(maxRecents))
        }
        saveRecents()
    }

    private func loadRecents() {
        recentArchives = UserDefaults.standard.stringArray(forKey: recentsKey) ?? []
        recentArchives.removeAll { !FileManager.default.fileExists(atPath: $0) }
    }

    private func saveRecents() {
        UserDefaults.standard.set(recentArchives, forKey: recentsKey)
    }

    // MARK: - Helpers

    private func parseDate(_ string: String) -> Date? {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd HH:mm:ss"
        if let date = formatter.date(from: string) { return date }
        formatter.dateFormat = "yyyy-MM-dd HH:mm"
        return formatter.date(from: string)
    }

    func totalSize() -> Int64 {
        entries.reduce(0) { $0 + $1.size }
    }

    func totalCompressedSize() -> Int64 {
        entries.reduce(0) { $0 + $1.compressedSize }
    }

    func compressionRatio() -> Double? {
        let raw = totalSize()
        let compressed = totalCompressedSize()
        guard raw > 0, compressed > 0 else { return nil }
        return 1.0 - Double(compressed) / Double(raw)
    }
}

enum ArchiveError: LocalizedError {
    case unsupportedFormat(String)
    case noArchiveOpen
    case listFailed(String)
    case extractionFailed(String)
    case creationFailed(String)
    case toolNotFound

    var errorDescription: String? {
        switch self {
        case .unsupportedFormat(let ext):
            return "Unsupported archive format: .\(ext)"
        case .noArchiveOpen:
            return "No archive is currently open"
        case .listFailed(let msg):
            return "Failed to list archive: \(msg)"
        case .extractionFailed(let msg):
            return "Extraction failed: \(msg)"
        case .creationFailed(let msg):
            return "Creation failed: \(msg)"
        case .toolNotFound:
            return "7-Zip tools not found. Install p7zip: brew install p7zip"
        }
    }
}
