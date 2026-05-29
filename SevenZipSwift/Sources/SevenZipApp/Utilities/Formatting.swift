import Foundation

enum Formatting {
    static func fileSize(_ bytes: Int64) -> String {
        let formatter = ByteCountFormatter()
        formatter.countStyle = .file
        return formatter.string(fromByteCount: bytes)
    }

    static func shortSize(_ bytes: Int64) -> String {
        if bytes < 1024 { return "\(bytes) B" }
        let kb = Double(bytes) / 1024.0
        if kb < 1024 { return String(format: "%.1f KB", kb) }
        let mb = kb / 1024.0
        if mb < 1024 { return String(format: "%.1f MB", mb) }
        return String(format: "%.2f GB", mb / 1024.0)
    }

    static func date(_ date: Date) -> String {
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .short
        return formatter.string(from: date)
    }

    static func dateCompact(_ date: Date) -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH:mm"
        return formatter.string(from: date)
    }
}

extension String {
    var fileExtension: String {
        (self as NSString).pathExtension.lowercased()
    }

    var isArchive: Bool {
        let exts: Set<String> = ["7z", "zip", "rar", "tar", "gz", "bz2", "xz", "tgz", "tbz2", "txz"]
        let ext = fileExtension
        if ext == "gz" && hasSuffix(".tar.gz") { return true }
        if ext == "bz2" && hasSuffix(".tar.bz2") { return true }
        if ext == "xz" && hasSuffix(".tar.xz") { return true }
        return exts.contains(ext)
    }

    var fileName: String {
        (self as NSString).lastPathComponent
    }

    var fileNameWithoutExtension: String {
        let name = fileName
        let dot = name.lastIndex(of: ".")
        return dot.map { String(name[name.startIndex..<$0]) } ?? name
    }
}

extension Date {
    var archiveFormatted: String {
        Formatting.dateCompact(self)
    }
}
