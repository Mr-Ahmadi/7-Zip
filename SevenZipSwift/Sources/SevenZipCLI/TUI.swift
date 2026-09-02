import Foundation
import CSevenZip
import CNcurses

// ══════════════════════════════════════════════════════════════════════
// 7-Zip TUI — Terminal archive manager
// ══════════════════════════════════════════════════════════════════════

// MARK: - Color Pairs

let CP_HEADER: Int32 = 1
let CP_STATUS: Int32 = 2
let CP_SELECTED: Int32 = 3
let CP_DIR: Int32 = 4
let CP_ARCHIVE: Int32 = 5
let CP_FILE: Int32 = 6
let CP_HELP: Int32 = 7
let CP_INPUT: Int32 = 8
let CP_TITLE: Int32 = 9

// MARK: - Data Types

struct ArchiveEntryRef {
    let name: String
    let size: Int64
    let compressedSize: Int64
    let isFolder: Bool
    let date: String
}

struct FileEntry {
    let name: String
    let path: String
    let isDir: Bool
    let size: Int64
    let date: String
    let isArchive: Bool
}

// MARK: - App State

enum TUIMode: String {
    case normal = "NORMAL"
    case command = "COMMAND"
    case search = "SEARCH"
}

enum TUIPane {
    case files
    case archive
}

final class TUIApp {
    private var entries: [FileEntry] = []
    private var archiveEntries: [ArchiveEntryRef] = []
    private var selectedIndex = 0
    private var scrollOffset = 0
    private var currentDir = FileManager.default.currentDirectoryPath
    private var openArchivePath: String?
    private var mode: TUIMode = .normal
    private var activePane: TUIPane = .files
    private var commandBuffer = ""
    private var searchBuffer = ""
    private var statusMessage = ""
    private var running = true
    private var archiver: UnsafeMutableRawPointer

    init() {
        archiver = archiver_create()
    }

    deinit {
        archiver_destroy(archiver)
    }

    func run() {
        nc_init()
        loadDirectory(currentDir)
        defer { nc_end() }

        while running {
            draw()
            handleInput()
        }
    }

    // MARK: - File System

    private func loadDirectory(_ path: String) {
        currentDir = path
        let fm = FileManager.default
        var items: [FileEntry] = []

        do {
            let contents = try fm.contentsOfDirectory(atPath: path)
            for name in contents {
                let fullPath = (path as NSString).appendingPathComponent(name)
                var isDir: ObjCBool = false
                guard fm.fileExists(atPath: fullPath, isDirectory: &isDir) else { continue }
                let attrs = try? fm.attributesOfItem(atPath: fullPath)
                let size = (attrs?[.size] as? Int64) ?? 0
                let date = (attrs?[.modificationDate] as? Date?)??.ISO8601Format() ?? ""
                let isArchive = archiver_is_archive(fullPath)
                let shortDate = String(date.prefix(10))
                items.append(FileEntry(name: name, path: fullPath, isDir: isDir.boolValue,
                                       size: isDir.boolValue ? 0 : size, date: shortDate,
                                       isArchive: isArchive))
            }
        } catch {
            statusMessage = "Error: \(error.localizedDescription)"
        }

        items.sort { a, b in
            if a.isDir != b.isDir { return a.isDir }
            return a.name.localizedCaseInsensitiveCompare(b.name) == .orderedAscending
        }

        entries = items
        selectedIndex = 0
        scrollOffset = 0
        activePane = .files
    }

    private func loadArchive(_ path: String) {
        guard archiver_is_archive(path) else {
            statusMessage = "Not an archive: \(path)"
            return
        }

        var err: UnsafeMutablePointer<CChar>?
        guard let list = archiver_list(archiver, path, nil, &err) else { return }
        if let e = err {
            let msg = String(cString: e)
            archiver_free_string(e)
            archiver_free_entries(list)
            statusMessage = "Failed to list archive: \(msg)"
            return
        }
        defer { archiver_free_entries(list) }

        var items: [ArchiveEntryRef] = []
        for i in 0..<Int(list.pointee.count) {
            let e = list.pointee.entries[i]
            items.append(ArchiveEntryRef(
                name: String(cString: e.name),
                size: e.size,
                compressedSize: e.compressedSize,
                isFolder: e.isFolder,
                date: String(cString: e.date)
            ))
        }

        archiveEntries = items
        openArchivePath = path
        activePane = .archive
        selectedIndex = 0
        statusMessage = "Archive loaded: \(items.count) entries"
    }

    // MARK: - Drawing

    private func draw() {
        nc_erase()
        drawHeader()
        drawContent()
        drawStatusBar()
        drawCommandBar()
        nc_refresh()
    }

    private func drawHeader() {
        let title = " 7-Zip TUI "
        let version = "v1.0"
        let cols = nc_cols()
        nc_attron(nc_A_BOLD())
        nc_attron(nc_COLOR_PAIR(CP_HEADER))
        nc_addstr(0, 0, String(repeating: " ", count: Int(cols)))
        nc_addstr(0, 0, title)
        nc_addstr(0, cols - Int32(version.count) - 2, " \(version) ")
        nc_attroff(nc_COLOR_PAIR(CP_HEADER))
        nc_attroff(nc_A_BOLD())
    }

    private func drawContent() {
        let rows = nc_rows()
        let contentRows = Int(rows) - 3
        guard contentRows > 0 else { return }

        switch activePane {
        case .files: drawFileList(contentRows)
        case .archive: drawArchiveContent(contentRows)
        }
    }

    private func drawFileList(_ maxRows: Int) {
        let startY = 1
        let cols = nc_cols()
        let pathWidth = Int(cols) - 4

        nc_attron(nc_A_REVERSE())
        let pathStr = " \(currentDir) "
        let padded = pathStr.padding(toLength: Int(cols), withPad: " ", startingAt: 0)
        nc_addstr(Int32(startY), 0, String(padded.prefix(max(0, Int(cols)))))
        nc_attroff(nc_A_REVERSE())

        let colY = startY + 1
        if colY < maxRows {
            nc_attron(nc_A_BOLD())
            let h = "\(padRight("Name", 40)) \(padLeft("Size", 10))  Date"
            nc_addstr(Int32(colY), 2, String(h.prefix(max(0, pathWidth))))
            nc_attroff(nc_A_BOLD())
        }

        guard !entries.isEmpty else {
            let msgY = colY + 2
            if msgY < maxRows { nc_addstr(Int32(msgY), 4, "(empty directory)") }
            return
        }

        if selectedIndex < scrollOffset { scrollOffset = selectedIndex }
        let visibleMax = maxRows - colY - 2
        if selectedIndex >= scrollOffset + visibleMax {
            scrollOffset = selectedIndex - visibleMax + 1
        }

        let visibleCount = min(entries.count - scrollOffset, maxRows - colY - 2)
        for i in 0..<max(0, visibleCount) {
            let idx = scrollOffset + i
            let rowY = colY + 2 + i
            guard rowY < maxRows, idx < entries.count else { break }
            let entry = entries[idx]
            let isSelected = idx == selectedIndex

            if isSelected { nc_attron(nc_A_REVERSE()) }

            let icon = entry.isDir ? "\u{1F4C1} " : (entry.isArchive ? "\u{1F4E6} " : "  ")
            let name = icon + entry.name
            let nameCol = padRight(name, min(40, pathWidth - 2))
            let sizeStr = entry.isDir ? "DIR" : formatSize(entry.size)
            let sizeCol = padLeft(sizeStr, 10)
            let line = " \(nameCol) \(sizeCol)  \(entry.date)"
            nc_addstr(Int32(rowY), 2, String(line.prefix(max(0, pathWidth))))

            if isSelected { nc_attroff(nc_A_REVERSE()) }
        }
    }

    private func drawArchiveContent(_ maxRows: Int) {
        let startY = 1
        let cols = nc_cols()
        let pathWidth = Int(cols) - 4

        nc_attron(nc_A_REVERSE())
        let archiveName = (openArchivePath ?? "") as NSString
        let headerStr = " Archive: \(archiveName.lastPathComponent) "
        let padded = headerStr.padding(toLength: Int(cols), withPad: " ", startingAt: 0)
        nc_addstr(Int32(startY), 0, String(padded.prefix(max(0, Int(cols)))))
        nc_attroff(nc_A_REVERSE())

        let colY = startY + 1
        nc_attron(nc_A_BOLD())
        let h = "\(padRight("Name", 40)) \(padLeft("Size", 10)) \(padLeft("Compressed", 10))  Date"
        nc_addstr(Int32(colY), 2, String(h.prefix(max(0, pathWidth))))
        nc_attroff(nc_A_BOLD())

        guard !archiveEntries.isEmpty else {
            let msgY = colY + 2
            if msgY < maxRows { nc_addstr(Int32(msgY), 4, "(empty archive)") }
            return
        }

        if selectedIndex < scrollOffset { scrollOffset = selectedIndex }
        let visibleMax = maxRows - colY - 2
        if selectedIndex >= scrollOffset + visibleMax {
            scrollOffset = selectedIndex - visibleMax + 1
        }

        let visibleCount = min(archiveEntries.count - scrollOffset, maxRows - colY - 2)
        for i in 0..<max(0, visibleCount) {
            let idx = scrollOffset + i
            let rowY = colY + 2 + i
            guard rowY < maxRows, idx < archiveEntries.count else { break }
            let entry = archiveEntries[idx]
            let isSelected = idx == selectedIndex

            if isSelected { nc_attron(nc_A_REVERSE()) }

            let icon = entry.isFolder ? "\u{1F4C1} " : "  "
            let name = icon + entry.name
            let nameCol = padRight(name, min(40, pathWidth - 2))
            let sizeCol = padLeft(entry.isFolder ? "DIR" : formatSize(entry.size), 10)
            let compCol = padLeft(entry.isFolder ? "DIR" : formatSize(entry.compressedSize), 10)
            let line = " \(nameCol) \(sizeCol) \(compCol)  \(entry.date)"
            nc_addstr(Int32(rowY), 2, String(line.prefix(max(0, pathWidth))))

            if isSelected { nc_attroff(nc_A_REVERSE()) }
        }
    }

    private func drawStatusBar() {
        let rows = nc_rows()
        let cols = nc_cols()
        let statusRow = rows - 2

        let modeStr = " \(mode.rawValue) "
        let info: String
        if activePane == .archive {
            info = " \(archiveEntries.count) entries  |  \((openArchivePath ?? "") as NSString).lastPathComponent"
        } else {
            info = " \(entries.count) items  |  \(currentDir)"
        }
        let full = mode == .normal ? "\(modeStr) \(info)" : modeStr
        let statusText = full.padding(toLength: Int(cols), withPad: " ", startingAt: 0)

        nc_attron(nc_COLOR_PAIR(CP_STATUS))
        nc_attron(nc_A_BOLD())
        nc_addstr(statusRow, 0, String(statusText.prefix(max(0, Int(cols)))))
        nc_attroff(nc_A_BOLD())
        nc_attroff(nc_COLOR_PAIR(CP_STATUS))
    }

    private func drawCommandBar() {
        let rows = nc_rows()
        let cols = nc_cols()
        let cmdRow = rows - 1

        switch mode {
        case .normal:
            let help = " [\u{2191}\u{2193}/jk] Nav  [Enter] Open  [b] Back  [x] Extract  [c] Create  [/] Search  [:] Cmd  [q] Quit"
            let padded = help.padding(toLength: Int(cols), withPad: " ", startingAt: 0)
            nc_attron(nc_A_REVERSE())
            nc_addstr(cmdRow, 0, String(padded.prefix(max(0, Int(cols)))))
            nc_attroff(nc_A_REVERSE())
        case .command:
            let prompt = ":\(commandBuffer)"
            nc_addstr(cmdRow, 0, String(repeating: " ", count: Int(cols)))
            nc_attron(nc_A_REVERSE())
            nc_addstr(cmdRow, 0, prompt)
            nc_attroff(nc_A_REVERSE())
            nc_curs_set(1)
            nc_move(cmdRow, Int32(1 + commandBuffer.count))
        case .search:
            let prompt = "/\(searchBuffer)"
            nc_addstr(cmdRow, 0, String(repeating: " ", count: Int(cols)))
            nc_attron(nc_A_REVERSE())
            nc_addstr(cmdRow, 0, prompt)
            nc_attroff(nc_A_REVERSE())
            nc_curs_set(1)
            nc_move(cmdRow, Int32(1 + searchBuffer.count))
        }
    }

    // MARK: - Input

    private func handleInput() {
        let ch = nc_getch()

        switch mode {
        case .normal: handleNormalInput(ch)
        case .command: handleCommandInput(ch)
        case .search: handleSearchInput(ch)
        }
    }

    private func handleNormalInput(_ ch: Int32) {
        let KD = nc_KEY_DOWN()
        let KU = nc_KEY_UP()
        let KE = nc_KEY_ENTER()
        let KB = nc_KEY_BACKSPACE()

        switch ch {
        case 113, 27: // q, ESC
            if activePane == .archive {
                openArchivePath = nil
                archiveEntries = []
                activePane = .files
                selectedIndex = 0
            } else {
                running = false
            }
        case 106, KD:   // j, Down
            moveDown()
        case 107, KU:   // k, Up
            moveUp()
        case KE, 10, 13:
            handleEnter()
        case 98, 127, KB: // b, Backspace
            if activePane == .files {
                goToParent()
            } else {
                openArchivePath = nil
                archiveEntries = []
                activePane = .files
            }
        case 58: // ':'
            mode = .command
            commandBuffer = ""
        case 47: // '/'
            mode = .search
            searchBuffer = ""
        case 120: // 'x'
            if activePane == .archive, openArchivePath != nil {
                performExtract()
            }
        case 99: // 'c'
            if activePane == .files {
                showCreatePrompt()
            }
        case 108: // 'l'
            if activePane == .files {
                loadDirectory(currentDir)
            }
        case 103: // 'g'
            selectedIndex = 0
            scrollOffset = 0
        case 71: // 'G'
            let count = activePane == .files ? entries.count : archiveEntries.count
            selectedIndex = max(0, count - 1)
        default:
            break
        }
    }

    private func handleCommandInput(_ ch: Int32) {
        let KB = nc_KEY_BACKSPACE()
        switch ch {
        case 27: // ESC
            mode = .normal
            commandBuffer = ""
            nc_curs_set(0)
        case 10, 13: // Enter
            executeCommand(commandBuffer)
            commandBuffer = ""
            mode = .normal
            nc_curs_set(0)
        case 127, KB:
            if !commandBuffer.isEmpty { commandBuffer.removeLast() }
        case 32...126:
            commandBuffer.append(Character(UnicodeScalar(UInt8(ch))))
        default:
            break
        }
    }

    private func handleSearchInput(_ ch: Int32) {
        let KB = nc_KEY_BACKSPACE()
        switch ch {
        case 27:
            mode = .normal
            searchBuffer = ""
            nc_curs_set(0)
        case 10, 13:
            performSearch(searchBuffer)
            searchBuffer = ""
            mode = .normal
            nc_curs_set(0)
        case 127, KB:
            if !searchBuffer.isEmpty { searchBuffer.removeLast() }
        case 32...126:
            searchBuffer.append(Character(UnicodeScalar(UInt8(ch))))
        default:
            break
        }
    }

    // MARK: - Navigation

    private func moveDown() {
        let count = activePane == .files ? entries.count : archiveEntries.count
        if selectedIndex < count - 1 { selectedIndex += 1 }
    }

    private func moveUp() {
        if selectedIndex > 0 { selectedIndex -= 1 }
    }

    private func handleEnter() {
        switch activePane {
        case .files:
            guard selectedIndex < entries.count else { return }
            let entry = entries[selectedIndex]
            if entry.isDir {
                loadDirectory(entry.path)
            } else if entry.isArchive {
                loadArchive(entry.path)
            }
        case .archive:
            break
        }
    }

    private func goToParent() {
        let parent = (currentDir as NSString).deletingLastPathComponent
        if parent != currentDir && FileManager.default.fileExists(atPath: parent) {
            loadDirectory(parent)
        }
    }

    // MARK: - Actions

    private func performExtract() {
        guard let archivePath = openArchivePath else { return }
        let dest = (archivePath as NSString).deletingPathExtension
        statusMessage = "Extracting to \(dest)..."
        draw()

        var err: UnsafeMutablePointer<CChar>?
        let ok = archiver_extract(archiver, archivePath, dest, nil, &err, nil, nil)
        if ok {
            statusMessage = "Extracted to \(dest)"
        } else {
            let msg = err.map { String(cString: $0) } ?? "Extraction failed"
            err.map { archiver_free_string($0) }
            statusMessage = "Error: \(msg)"
        }
    }

    private func showCreatePrompt() {
        mode = .command
        commandBuffer = "create "
    }

    private func executeCommand(_ cmd: String) {
        let parts = cmd.split(separator: " ", maxSplits: 2, omittingEmptySubsequences: true).map(String.init)
        guard let command = parts.first?.lowercased() else { return }

        switch command {
        case "q", "quit":
            running = false
        case "h", "help":
            statusMessage = "j/k/arrows=navigate, Enter=open, b=back, x=extract, :=command, /=search, q=quit"
        case "cd":
            if parts.count >= 2 {
                let dir = parts[1]
                let expanded = (dir as NSString).expandingTildeInPath
                var isDir: ObjCBool = false
                if FileManager.default.fileExists(atPath: expanded, isDirectory: &isDir), isDir.boolValue {
                    loadDirectory(expanded)
                } else {
                    statusMessage = "Directory not found: \(dir)"
                }
            } else {
                statusMessage = "Usage: cd <directory>"
            }
        case "create":
            if parts.count >= 2 {
                let archivePath = parts[1]
                let expanded = (archivePath as NSString).expandingTildeInPath
                let selectedFiles = currentDirFiles()
                guard !selectedFiles.isEmpty else {
                    statusMessage = "No files in current directory"
                    return
                }
                createArchive(at: expanded, files: selectedFiles)
            } else {
                statusMessage = "Usage: create <archive-path>"
            }
        case "extract":
            if openArchivePath != nil {
                performExtract()
            } else {
                statusMessage = "No archive open"
            }
        default:
            statusMessage = "Unknown command: \(command)"
        }
    }

    private func currentDirFiles() -> [String] {
        entries.filter { !$0.isDir }.map(\.path)
    }

    private func createArchive(at path: String, files: [String]) {
        statusMessage = "Creating archive..."
        draw()

        let cstrs: [UnsafeMutablePointer<CChar>?] = files.map { strdup($0) }
        defer { cstrs.forEach { if let p = $0 { free(p) } } }
        let cfiles: [UnsafePointer<CChar>?] = cstrs.map { UnsafePointer($0) }

        // An empty format lets the archiver derive it from the destination,
        // which handles two-part extensions like .tar.gz correctly.
        var err: UnsafeMutablePointer<CChar>?

        let ok = archiver_create_archive(archiver, cfiles, Int32(files.count),
                                         path, "", 5, nil, &err, nil, nil)
        if ok {
            statusMessage = "Archive created: \(path)"
            loadDirectory(currentDir)
        } else {
            let msg = err.map { String(cString: $0) } ?? "Creation failed"
            err.map { archiver_free_string($0) }
            statusMessage = "Error: \(msg)"
        }
    }

    private func performSearch(_ query: String) {
        guard !query.isEmpty else { return }
        let q = query.lowercased()
        switch activePane {
        case .files:
            for (i, entry) in entries.enumerated() {
                if entry.name.lowercased().contains(q) {
                    selectedIndex = i
                    return
                }
            }
        case .archive:
            for (i, entry) in archiveEntries.enumerated() {
                if entry.name.lowercased().contains(q) {
                    selectedIndex = i
                    return
                }
            }
        }
        statusMessage = "No match: \(query)"
    }

    // MARK: - Helpers

    private func padRight(_ s: String, _ len: Int) -> String {
        if s.count >= len { return String(s.prefix(len)) }
        return s + String(repeating: " ", count: len - s.count)
    }

    private func padLeft(_ s: String, _ len: Int) -> String {
        if s.count >= len { return String(s.suffix(len)) }
        return String(repeating: " ", count: len - s.count) + s
    }

    private func formatSize(_ size: Int64) -> String {
        if size < 1024 { return "\(size)B" }
        let kb = Double(size) / 1024
        if kb < 1024 { return String(format: "%.0fK", kb) }
        let mb = kb / 1024
        if mb < 1024 { return String(format: "%.1fM", mb) }
        let gb = mb / 1024
        return String(format: "%.1fG", gb)
    }
}

// MARK: - Entry Point

func runTUI() {
    let app = TUIApp()
    app.run()
}
