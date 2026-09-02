import Foundation
import CSevenZip

// ── Helpers ──────────────────────────────────────────────────────────────

func printStderr(_ msg: String) { fputs(msg + "\n", stderr) }

/// Collapses the blank lines 7za pads its diagnostics with.
func cleanMessage(_ raw: String) -> String {
    let lines = raw.split(separator: "\n", omittingEmptySubsequences: false)
        .map { $0.trimmingCharacters(in: .whitespaces) }
        .filter { !$0.isEmpty }
    return lines.isEmpty ? raw.trimmingCharacters(in: .whitespacesAndNewlines)
                         : lines.joined(separator: "\n")
}

/// Redraws progress in place on stderr. Skipped when stderr is redirected,
/// so piped output does not fill up with partial progress lines.
let progressCallback: CProgressCallback = { pct, _ in
    guard isatty(STDERR_FILENO) == 1 else { return }
    fputs("\rProgress: \(pct)%", stderr)
    fflush(stderr)
}

/// Clears the progress line before printing the final result.
func clearProgress() {
    guard isatty(STDERR_FILENO) == 1 else { return }
    fputs("\r\u{1B}[K", stderr)
    fflush(stderr)
}

func printHelp() {
    print("""
Usage: 7z <command> [options] <archive> [files...]

Commands:
  list, ls, l    <archive>           List archive contents
  extract, x     <archive> [dir]     Extract archive
  create, a      <archive> <files>   Create archive

Options:
  --output, -o   <dir>               Output directory (extract)
  --format, -t   <format>            Archive format: 7z, zip, tar, tar.gz, tar.bz2,
                                     tar.xz (default: from the archive extension)
  --level, -mx   <0-9>              Compression level (default: 5)
  --password, -p <password>          Encrypt/decrypt with password
  --verbose, -V                      Verbose output
  --tui                              Launch interactive TUI
  --tool                             Show the archive engine being used
  --help, -h                         Show this help
  --version, -v                      Show version

Run without arguments to start the interactive TUI.

Examples:
  7z list archive.7z
  7z extract archive.7z ./output
  7z create myfiles.zip file1.txt file2.txt
  7z create -t 7z -mx 9 -p secret archive.7z folder/
  7z              (launch TUI)
""")
}

// ── Argument Parsing ─────────────────────────────────────────────────────

enum Command { case list, extract, create, help, version, tool }

struct Args {
    var command: Command?
    var archive: String?
    var files: [String] = []
    var outputDir = FileManager.default.currentDirectoryPath
    /// Empty means "derive the format from the destination extension".
    var format = ""
    var level = 5
    var password = ""
    var verbose = false
    var tui = false
}

func parseArgs() -> Args {
    let args = CommandLine.arguments
    var a = Args()
    var positional: [String] = []
    var explicitHelp = false
    var i = 1

    while i < args.count {
        let arg = args[i]
        switch arg {
        case "--help", "-h": explicitHelp = true; i += 1
        case "--version", "-v": a.command = .version; i += 1
        case "--verbose", "-V": a.verbose = true; i += 1
        case "--tui": a.tui = true; i += 1
        case "--tool": a.command = .tool; i += 1
        case "--output", "-o":
            if i + 1 < args.count { a.outputDir = args[i + 1]; i += 2 } else { i += 1 }
        case "--format", "-t":
            if i + 1 < args.count { a.format = args[i + 1]; i += 2 } else { i += 1 }
        case "--level", "-mx":
            if i + 1 < args.count { a.level = Int(args[i + 1]) ?? 5; i += 2 } else { i += 1 }
        case "--password", "-p":
            if i + 1 < args.count { a.password = args[i + 1]; i += 2 } else { i += 1 }
        default:
            if arg.hasPrefix("-") {
                printStderr("error: unknown option '\(arg)'")
                printHelp()
                exit(1)
            }
            positional.append(arg)
            i += 1
        }
    }

    if explicitHelp { a.command = .help; return a }
    if a.command == .version || a.command == .tool { return a }

    if positional.isEmpty || a.tui {
        a.tui = true
        return a
    }

    switch positional[0].lowercased() {
    case "list", "ls", "l":
        a.command = .list
        if positional.count >= 2 { a.archive = positional[1] }
    case "extract", "x":
        a.command = .extract
        if positional.count >= 2 { a.archive = positional[1] }
        if positional.count >= 3 { a.outputDir = positional[2] }
    case "create", "a":
        a.command = .create
        if positional.count >= 2 { a.archive = positional[1] }
        if positional.count >= 3 { a.files = Array(positional[2...]) }
    case "help":
        a.command = .help
        return a
    default:
        printStderr("error: unknown command '\(positional[0])'")
        printHelp()
        exit(1)
    }

    if a.archive == nil {
        printStderr("error: missing archive path")
        exit(1)
    }
    return a
}

// ── Entry Point ──────────────────────────────────────────────────────────

@main
struct App {
    static func main() {
        setvbuf(stdout, nil, _IOLBF, 0)
        let args = parseArgs()

        if args.tui {
            runTUI()
            return
        }

        guard let cmd = args.command else {
            printHelp(); return
        }
        switch cmd {
        case .help: printHelp(); return
        case .version: print("7-Zip CLI 1.0.0"); return
        case .tool: cmdTool(); return
        case .list: cmdList(args: args)
        case .extract: cmdExtract(args: args)
        case .create: cmdCreate(args: args)
        }
    }
}

// ── Commands ─────────────────────────────────────────────────────────────

/// Reports which 7za/7z binary the archiver resolved to. The app bundle ships
/// its own copy, so this is the quickest way to tell an embedded engine from a
/// system one when something misbehaves.
func cmdTool() {
    guard let cStr = archiver_find_tool() else {
        printStderr("No archive engine found.")
        printStderr("Install p7zip (brew install p7zip) or run from the app bundle.")
        exit(1)
    }
    print(String(cString: cStr))
    archiver_free_string(cStr)
}

func cmdList(args: Args) {
    guard let path = args.archive else { printStderr("error: no archive specified"); exit(1) }
    guard archiver_is_archive(path) else {
        printStderr("error: unsupported archive: \(path)")
        exit(1)
    }
    let handle = archiver_create()
    defer { archiver_destroy(handle) }
    var err: UnsafeMutablePointer<CChar>?
    guard let list = archiver_list(handle, path, args.password.isEmpty ? nil : args.password, &err)
    else { exit(1) }
    if let e = err {
        printStderr("error: \(cleanMessage(String(cString: e)))")
        archiver_free_string(e)
        archiver_free_entries(list)
        exit(1)
    }
    defer { archiver_free_entries(list) }

    let count = Int(list.pointee.count)
    print("Archive: \(URL(fileURLWithPath: path).lastPathComponent)")
    print("\(count) entr\(count == 1 ? "y" : "ies")")
    print(String(repeating: "-", count: 72))
    print("\(leftPad("Name", 40))\(rightPad("Size", 12))\(rightPad("Compressed", 12))  Date")
    print(String(repeating: "-", count: 72))
    for i in 0..<count {
        let e = list.pointee.entries[i]
        let name = String(cString: e.name)
        let date = String(cString: e.date)
        let prefix = e.isFolder ? "[DIR] " : ""
        print("\(leftPad(prefix + name, 40))\(rightPad(formatSize(e.size), 12))\(rightPad(formatSize(e.compressedSize), 12))  \(date)")
    }
}

/// Pads to `len` for column alignment. Long names overflow rather than being
/// truncated — a listing that hides part of a filename is worse than a ragged
/// column.
func leftPad(_ s: String, _ len: Int) -> String {
    if s.count >= len { return s + " " }
    return s + String(repeating: " ", count: len - s.count)
}

func rightPad(_ s: String, _ len: Int) -> String {
    if s.count >= len { return String(s.suffix(len)) }
    return String(repeating: " ", count: len - s.count) + s
}

func formatSize(_ size: Int64) -> String {
    if size < 1024 { return "\(size)" }
    let kb = Double(size) / 1024
    if kb < 1024 { return String(format: "%.1fK", kb) }
    let mb = kb / 1024
    if mb < 1024 { return String(format: "%.1fM", mb) }
    let gb = mb / 1024
    return String(format: "%.1fG", gb)
}

func cmdExtract(args: Args) {
    guard let path = args.archive else { printStderr("error: no archive specified"); exit(1) }
    guard archiver_is_archive(path) else {
        printStderr("error: unsupported archive: \(path)")
        exit(1)
    }
    let handle = archiver_create()
    defer { archiver_destroy(handle) }
    let dest = args.outputDir
    try? FileManager.default.createDirectory(atPath: dest, withIntermediateDirectories: true)
    print("Extracting to \(dest)...")
    var err: UnsafeMutablePointer<CChar>?
    let ok = archiver_extract(handle, path, dest, args.password.isEmpty ? nil : args.password, &err, progressCallback, nil)
    clearProgress()
    if ok {
        print("Done!")
    } else {
        let msg = err.map { cleanMessage(String(cString: $0)) } ?? "extraction failed"
        err.map { archiver_free_string($0) }
        printStderr("error: \(msg)")
        exit(1)
    }
}

func cmdCreate(args: Args) {
    guard let dest = args.archive else { printStderr("error: no archive specified"); exit(1) }
    guard !args.files.isEmpty else {
        printStderr("error: no input files")
        exit(1)
    }
    for f in args.files {
        guard FileManager.default.fileExists(atPath: f) else {
            printStderr("error: file not found: \(f)")
            exit(1)
        }
    }
    let handle = archiver_create()
    defer { archiver_destroy(handle) }
    let cstrs: [UnsafeMutablePointer<CChar>?] = args.files.map { strdup($0) }
    defer { cstrs.forEach { if let p = $0 { free(p) } } }
    let cfiles: [UnsafePointer<CChar>?] = cstrs.map { UnsafePointer($0) }
    print("Creating \(dest)...")
    var err: UnsafeMutablePointer<CChar>?
    let ok = archiver_create_archive(handle, cfiles, Int32(args.files.count), dest, args.format, Int32(args.level), args.password.isEmpty ? nil : args.password, &err, progressCallback, nil)
    clearProgress()
    if ok {
        print("Done!")
    } else {
        let msg = err.map { cleanMessage(String(cString: $0)) } ?? "creation failed"
        err.map { archiver_free_string($0) }
        printStderr("error: \(msg)")
        exit(1)
    }
}
