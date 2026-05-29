import Foundation

enum ArchiveFormat: String, CaseIterable, Identifiable {
    case zip = "zip"
    case sevenZ = "7z"
    case rar = "rar"
    case tar = "tar"
    case tarGz = "tar.gz"
    case tarBz2 = "tar.bz2"
    case tarXz = "tar.xz"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .zip:     return "Zip (Compatible)"
        case .sevenZ:  return "7z (Best Compression)"
        case .rar:     return "RAR"
        case .tar:     return "Tar (No Compression)"
        case .tarGz:   return "Tar.gz"
        case .tarBz2:  return "Tar.bz2"
        case .tarXz:   return "Tar.xz"
        }
    }

    var extensions: [String] {
        switch self {
        case .zip:    return ["zip"]
        case .sevenZ: return ["7z"]
        case .rar:    return ["rar"]
        case .tar:    return ["tar"]
        case .tarGz:  return ["tar.gz", "tgz"]
        case .tarBz2: return ["tar.bz2", "tbz2"]
        case .tarXz:  return ["tar.xz", "txz"]
        }
    }

    var canCreate: Bool {
        switch self {
        case .rar: return false
        default:   return true
        }
    }

    static var creatableFormats: [ArchiveFormat] {
        allCases.filter(\.canCreate)
    }
}

enum ArchiveAction {
    case idle
    case loading
    case extracting(Double)
    case creating(Double)
    case success(String)
    case failure(String)
}

extension ArchiveAction: Equatable {
    static func == (lhs: ArchiveAction, rhs: ArchiveAction) -> Bool {
        switch (lhs, rhs) {
        case (.idle, .idle), (.loading, .loading): return true
        case (.extracting(let a), .extracting(let b)): return a == b
        case (.creating(let a), .creating(let b)): return a == b
        case (.success(let a), .success(let b)): return a == b
        case (.failure(let a), .failure(let b)): return a == b
        default: return false
        }
    }
}
