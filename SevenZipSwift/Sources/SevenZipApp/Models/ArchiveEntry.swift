import Foundation

struct ArchiveEntry: Identifiable, Hashable {
    let id = UUID()
    let name: String
    let path: String
    let size: Int64
    let compressedSize: Int64
    let isFolder: Bool
    let date: Date

    init(name: String, path: String, size: Int64 = 0, compressedSize: Int64 = 0,
         isFolder: Bool = false, date: Date = Date()) {
        self.name = name
        self.path = path
        self.size = size
        self.compressedSize = compressedSize
        self.isFolder = isFolder
        self.date = date
    }
}
