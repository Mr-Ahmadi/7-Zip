import SwiftUI

struct FileRowView: View {
    let name: String
    let size: Int64
    let compressedSize: Int64
    let date: Date
    let isFolder: Bool

    init(entry: ArchiveEntry) {
        self.name = entry.name
        self.size = entry.size
        self.compressedSize = entry.compressedSize
        self.date = entry.date
        self.isFolder = entry.isFolder
    }

    init(node: TreeNode) {
        self.name = node.name
        self.size = node.size
        self.compressedSize = node.compressedSize
        self.date = node.date
        self.isFolder = node.isFolder
    }

    var body: some View {
        HStack(spacing: 10) {
            icon
                .frame(width: 22, alignment: .center)

            VStack(alignment: .leading, spacing: 1) {
                Text(name)
                    .lineLimit(1)
                    .font(.body)
                if isFolder {
                    Text("Folder")
                        .font(.caption2)
                        .foregroundStyle(.tertiary)
                }
            }

            Spacer()

            if size > 0 {
                Text(Formatting.shortSize(size))
                    .font(.callout.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .frame(width: 80, alignment: .trailing)
            }

            if compressedSize > 0 {
                Text(Formatting.shortSize(compressedSize))
                    .font(.callout.monospacedDigit())
                    .foregroundStyle(.tertiary)
                    .frame(width: 80, alignment: .trailing)
            }

            Text(Formatting.dateCompact(date))
                .font(.caption)
                .foregroundStyle(.tertiary)
                .frame(width: 130, alignment: .trailing)
        }
        .padding(.vertical, 2)
    }

    @ViewBuilder
    private var icon: some View {
        if isFolder {
            Image(systemName: "folder.fill")
                .foregroundStyle(.blue)
        } else {
            Image(systemName: iconName)
                .foregroundStyle(iconColor)
        }
    }

    private var iconName: String {
        switch name.fileExtension {
        case "txt", "md", "csv", "json", "xml", "yml", "yaml", "swift", "py", "js", "ts", "cpp", "h", "c":
            return "doc.text.fill"
        case "pdf":
            return "book.pages.fill"
        case "jpg", "jpeg", "png", "gif", "webp", "bmp", "tiff":
            return "photo.fill"
        case "mp4", "mov", "avi", "mkv", "webm":
            return "film.fill"
        case "mp3", "wav", "aac", "flac", "ogg":
            return "music.note"
        case "zip", "7z", "rar", "tar", "gz", "bz2", "xz":
            return "archivebox.fill"
        case "dmg", "pkg", "app":
            return "shippingbox.fill"
        default:
            return "doc.fill"
        }
    }

    private var iconColor: Color {
        switch name.fileExtension {
        case "txt", "md":       return .blue
        case "pdf":             return .red
        case "jpg", "jpeg", "png", "gif", "webp": return .green
        case "mp4", "mov":      return .orange
        case "mp3", "wav":      return .purple
        case "zip", "7z", "rar": return .cyan
        case "dmg", "pkg":      return .indigo
        case "swift", "py", "js", "ts", "cpp", "h": return .mint
        default:                return .secondary
        }
    }
}
