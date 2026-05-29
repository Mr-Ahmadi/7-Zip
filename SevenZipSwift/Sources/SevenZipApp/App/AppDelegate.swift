import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {

    // ── AppleEvent handler (prevents SwiftUI from creating extra windows) ──
    func applicationWillFinishLaunching(_ notification: Notification) {
        NSAppleEventManager.shared().setEventHandler(
            self,
            andSelector: #selector(handleOpenEvent(_:withReply:)),
            forEventClass: AEEventClass(kCoreEventClass),
            andEventID: AEEventID(kAEOpenDocuments)
        )
    }

    @objc func handleOpenEvent(_ event: NSAppleEventDescriptor, withReply reply: NSAppleEventDescriptor) {
        guard let desc = event.paramDescriptor(forKeyword: keyDirectObject) else { return }
        for i in 1...desc.numberOfItems {
            if let urlStr = desc.atIndex(i)?.stringValue,
               let url = URL(string: urlStr),
               url.isFileURL {
                openArchive(url.path)
                break
            }
        }
    }

    // ── Delegate fallback (in case AppleEvent handler is not called) ──
    func application(_ application: NSApplication, openFile filename: String) -> Bool {
        openArchive(filename)
        return true
    }

    func application(_ sender: NSApplication, openFiles filenames: [String]) {
        guard let f = filenames.first else { return }
        openArchive(f)
    }

    // ── Shared open logic ──
    private func openArchive(_ path: String) {
        // Dispatch async to let the window finish appearing first
        DispatchQueue.main.async {
            Task { @MainActor in
                try? await ArchiveService.shared.openArchive(at: path)
            }
        }
    }
}
