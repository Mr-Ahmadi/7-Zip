import Foundation

/// A node in the archive's file tree.
struct TreeNode: Identifiable {
    let id: String
    let name: String
    let path: String
    let entry: ArchiveEntry?
    let isFolder: Bool
    var children: [TreeNode]

    /// Optional children for SwiftUI OutlineGroup compatibility
    var outlineChildren: [TreeNode]? { children.isEmpty ? nil : children }

    var size: Int64 {
        if let entry, !entry.isFolder { return entry.size }
        return children.reduce(0) { $0 + $1.size }
    }

    var compressedSize: Int64 {
        if let entry, !entry.isFolder { return entry.compressedSize }
        return children.reduce(0) { $0 + $1.compressedSize }
    }

    var date: Date {
        entry?.date ?? Date()
    }

    /// Build a tree from a flat list of archive entries.
    static func buildTree(from entries: [ArchiveEntry]) -> [TreeNode] {
        // 1. Map every unique path to its entry
        var entryMap: [String: ArchiveEntry] = [:]
        for e in entries { entryMap[e.path] = e }

        // 2. Collect all unique directory paths (implicit + explicit)
        var dirs = Set<String>()
        for e in entries {
            if e.isFolder { dirs.insert(e.path) }
            let parent = (e.path as NSString).deletingLastPathComponent
            if parent != "." && !parent.isEmpty {
                // Walk up adding every component
                var p = ""
                for comp in parent.split(separator: "/", omittingEmptySubsequences: true) {
                    if !p.isEmpty { p += "/" }
                    p += comp
                    dirs.insert(p)
                }
            }
        }

        // 3. Create nodes for everything
        var nodeMap: [String: TreeNode] = [:]
        for d in dirs {
            nodeMap[d] = TreeNode(
                id: d,
                name: (d as NSString).lastPathComponent,
                path: d,
                entry: nil,
                isFolder: true,
                children: []
            )
        }
        for (p, e) in entryMap {
            nodeMap[p] = TreeNode(
                id: p, name: e.name, path: p,
                entry: e, isFolder: e.isFolder, children: []
            )
        }

        // 4. Build parent→children map
        var childMap: [String: [String]] = [:]
        for path in nodeMap.keys {
            let parent = (path as NSString).deletingLastPathComponent
            if parent != "." && !parent.isEmpty {
                childMap[parent, default: []].append(path)
            }
        }

        // 5. Recursively build tree (top-down to avoid value-type copy issues)
        func node(for path: String) -> TreeNode {
            guard var n = nodeMap[path] else {
                return TreeNode(id: path, name: (path as NSString).lastPathComponent,
                                path: path, entry: nil, isFolder: true, children: [])
            }
            if let kids = childMap[path] {
                n.children = kids.sorted().map { node(for: $0) }
            }
            return n
        }

        let rootPaths = nodeMap.keys.filter {
            let p = ($0 as NSString).deletingLastPathComponent
            return p == "." || p.isEmpty
        }

        // 6. Sort: folders first, then alphabetical
        func sorted(_ ns: [TreeNode]) -> [TreeNode] {
            ns.sorted { a, b in
                if a.isFolder != b.isFolder { return a.isFolder }
                return a.name.localizedCompare(b.name) == .orderedAscending
            }.map { n in
                var n = n
                n.children = sorted(n.children)
                return n
            }
        }

        return sorted(rootPaths.sorted().map { node(for: $0) })
    }
}
