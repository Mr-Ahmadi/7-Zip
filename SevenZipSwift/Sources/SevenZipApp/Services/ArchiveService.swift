import Foundation
import CSevenZip

/// Parse a date string from 7za output into a Date.
private func parseArchiveDate(_ string: String) -> Date? {
    let formatter = DateFormatter()
    formatter.locale = Locale(identifier: "en_US_POSIX")
    formatter.dateFormat = "yyyy-MM-dd HH:mm:ss"
    if let date = formatter.date(from: string) { return date }
    formatter.dateFormat = "yyyy-MM-dd HH:mm"
    return formatter.date(from: string)
}

/// Wraps the C archiver pointer and runs blocking C calls off the main actor.
private final class ArchiverActor: @unchecked Sendable {
    let ptr: UnsafeMutableRawPointer

    init() { ptr = archiver_create() }
    deinit { archiver_destroy(ptr) }
    func cancel() { archiver_cancel(ptr) }

    func list(_ path: String, password: String? = nil) throws -> [ArchiveEntry] {
        var errorPtr: UnsafeMutablePointer<CChar>?
        let list = archiver_list(ptr, path, password, &errorPtr)
        if let errorPtr {
            let msg = String(cString: errorPtr)
            archiver_free_string(errorPtr)
            throw ArchiveError.listFailed(msg)
        }
        defer { archiver_free_entries(list) }
        let listRef = list!
        var result: [ArchiveEntry] = []
        for i in 0..<Int(listRef.pointee.count) {
            let e = listRef.pointee.entries[i]
            result.append(ArchiveEntry(
                name: String(cString: e.name),
                path: String(cString: e.path),
                size: e.size,
                compressedSize: e.compressedSize,
                isFolder: e.isFolder,
                date: parseArchiveDate(String(cString: e.date)) ?? Date()
            ))
        }
        return result
    }

    func extract(_ path: String, dest: String, password: String?,
                 onProgress: @escaping (Double) -> Void) throws {
        var errorPtr: UnsafeMutablePointer<CChar>?
        let progressHolder = ProgressHolder(callback: onProgress)
        let ctx = Unmanaged.passUnretained(progressHolder).toOpaque()
        let ok = archiver_extract(ptr, path, dest, password, &errorPtr, progressCb, ctx)
        if let errorPtr {
            let msg = String(cString: errorPtr)
            archiver_free_string(errorPtr)
            if !ok { throw ArchiveError.extractionFailed(msg) }
        } else if !ok {
            throw ArchiveError.extractionFailed("Extraction failed")
        }
    }

    func create(files: [String], dest: String, format: String,
                level: Int, password: String?,
                onProgress: @escaping (Double) -> Void) throws {
        let cFiles: [UnsafePointer<CChar>?] = files.map { f in
            guard let c = strdup(f) else { return nil }
            return UnsafePointer(c)
        }
        defer { cFiles.forEach { p in p.map { free(UnsafeMutablePointer(mutating: $0)) } } }
        var errorPtr: UnsafeMutablePointer<CChar>?
        let progressHolder = ProgressHolder(callback: onProgress)
        let ctx = Unmanaged.passUnretained(progressHolder).toOpaque()
        let ok = archiver_create_archive(ptr, cFiles, Int32(files.count),
                                         dest, format, Int32(level),
                                         password, &errorPtr, progressCb, ctx)
        if let errorPtr {
            let msg = String(cString: errorPtr)
            archiver_free_string(errorPtr)
            if !ok { throw ArchiveError.creationFailed(msg) }
        } else if !ok {
            throw ArchiveError.creationFailed("Creation failed")
        }
    }
}

/// Context holder for C progress callbacks (no closure capture in C function ptr).
private final class ProgressHolder {
    let callback: (Double) -> Void
    init(callback: @escaping (Double) -> Void) { self.callback = callback }
}

/// C function pointer used by archiver_extract / archiver_create_archive.
private let progressCb: CProgressCallback = { pct, ctx in
    let holder = Unmanaged<ProgressHolder>.fromOpaque(ctx!).takeUnretainedValue()
    DispatchQueue.main.async { holder.callback(Double(pct) / 100.0) }
}

/// Runs blocking C archiver calls on a background queue.
private let archiveQueue = DispatchQueue(label: "com.sevenzip.archive", qos: .userInitiated)

@MainActor
final class ArchiveService: ObservableObject {
    static let shared = ArchiveService()

    @Published var action: ArchiveAction = .idle
    @Published var entries: [ArchiveEntry] = []
    @Published var archivePath: String = ""
    @Published var recentArchives: [String] = []

    /// Set by AppDelegate when opened via "Open With" — consumed by ContentView.
    @Published var pendingPath: String?

    /// When true, the UI should present a password prompt for an encrypted archive.
    @Published var requiresPassword = false
    /// The path of the archive that needs a password to be opened.
    @Published var pendingArchivePath: String = ""

    private let archiver = ArchiverActor()
    private let recentsKey = "recentArchives"
    private let maxRecents = 8

    private init() { loadRecents() }

    var isArchiveOpen: Bool { !archivePath.isEmpty }
    var archiveName: String { archivePath.fileName }

    var tree: [TreeNode] {
        TreeNode.buildTree(from: entries)
    }

    var isToolAvailable: Bool { archiver_is_tool_available() }

    var toolPath: String {
        guard let cStr = archiver_find_tool() else { return "" }
        let str = String(cString: cStr)
        archiver_free_string(cStr)
        return str
    }

    // MARK: - List Archive

    /// Try to open an archive. If it's encrypted and no password (or wrong password) was given,
    /// sets `requiresPassword` so the UI can prompt the user.
    func openArchive(at path: String, password: String? = nil) async throws {
        guard archiver_is_archive(path) else {
            throw ArchiveError.unsupportedFormat(path.fileExtension)
        }

        // Try listing without password first if none provided
        try await performOpen(path: path, password: password)
    }

    /// Retry opening the pending encrypted archive with a password.
    /// Returns nil on success, or an error message on wrong password.
    func retryWithPassword(_ password: String) async -> String? {
        let path = pendingArchivePath
        do {
            try await performOpen(path: path, password: password)
            requiresPassword = false
            pendingArchivePath = ""
            return nil
        } catch {
            return error.localizedDescription
        }
    }

    /// Cancel the password prompt and close.
    func cancelPasswordPrompt() {
        requiresPassword = false
        pendingArchivePath = ""
        archivePath = ""
        action = .idle
    }

    private func performOpen(path: String, password: String?) async throws {
        action = .loading
        archivePath = path
        addRecent(path)

        do {
            let entries = try await withCheckedThrowingContinuation { (c: CheckedContinuation<[ArchiveEntry], Error>) in
                archiveQueue.async { [archiver] in
                    do {
                        let result = try archiver.list(path, password: password)
                        DispatchQueue.main.async { c.resume(returning: result) }
                    } catch {
                        DispatchQueue.main.async { c.resume(throwing: error) }
                    }
                }
            }
            self.entries = entries
            action = .idle
        } catch let error as ArchiveError {
            if password == nil && error.isEncrypted {
                requiresPassword = true
                pendingArchivePath = path
                action = .idle
                return
            }
            action = .failure(error.localizedDescription)
            throw error
        }
    }

    func closeArchive() {
        archivePath = ""
        entries = []
        action = .idle
        archiver.cancel()
    }

    // MARK: - Extract

    func extractArchive(to destination: String, password: String = "") async throws {
        guard !archivePath.isEmpty else { throw ArchiveError.noArchiveOpen }
        action = .extracting(0)
        let pwd = password.isEmpty ? nil : password

        let archivePathValue = archivePath

        return try await withCheckedThrowingContinuation { (c: CheckedContinuation<Void, Error>) in
            archiveQueue.async { [archiver] in
                do {
                    try archiver.extract(archivePathValue, dest: destination,
                                         password: pwd, onProgress: { pct in
                        DispatchQueue.main.async {
                            ArchiveService.shared.action = .extracting(pct)
                        }
                    })
                    DispatchQueue.main.async {
                        ArchiveService.shared.action = .success("Extracted to \(destination.fileName)")
                        c.resume()
                    }
                } catch {
                    DispatchQueue.main.async {
                        ArchiveService.shared.action = .failure(error.localizedDescription)
                        c.resume(throwing: error)
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
        let pwd = password.isEmpty ? nil : password

        return try await withCheckedThrowingContinuation { (c: CheckedContinuation<Void, Error>) in
            archiveQueue.async { [archiver] in
                do {
                    try archiver.create(files: files, dest: destination,
                                        format: format.rawValue,
                                        level: compressionLevel,
                                        password: pwd, onProgress: { pct in
                        DispatchQueue.main.async {
                            ArchiveService.shared.action = .creating(pct)
                        }
                    })
                    DispatchQueue.main.async {
                        ArchiveService.shared.action = .success("Archive created")
                        c.resume()
                    }
                } catch {
                    DispatchQueue.main.async {
                        ArchiveService.shared.action = .failure(error.localizedDescription)
                        c.resume(throwing: error)
                    }
                }
            }
        }
    }

    func cancel() {
        archiver.cancel()
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

    func clearRecents() {
        recentArchives.removeAll()
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
    case needsPassword

    var errorDescription: String? {
        switch self {
        case .unsupportedFormat(let ext): return "Unsupported archive format: .\(ext)"
        case .noArchiveOpen: return "No archive is currently open"
        case .listFailed(let msg): return msg
        case .extractionFailed(let msg): return "Extraction failed: \(msg)"
        case .creationFailed(let msg): return "Creation failed: \(msg)"
        case .toolNotFound: return "Archive engine not available. The app bundle may be corrupted."
        case .needsPassword: return "This archive is encrypted. Please provide a password."
        }
    }

    var isEncrypted: Bool {
        if case .needsPassword = self { return true }
        if case .listFailed(let msg) = self {
            let lower = msg.lowercased()
            return lower.contains("encrypted") || lower.contains("wrong password") || lower.contains("headers error")
        }
        return false
    }
}
