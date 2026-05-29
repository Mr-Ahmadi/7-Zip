import SwiftUI

@main
struct SevenZipApp: App {
    @StateObject private var service = ArchiveService.shared

    #if os(macOS)
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    #endif

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(service)
                .frame(minWidth: 640, minHeight: 440)
        }
        .windowResizability(.contentMinSize)
        .handlesExternalEvents(matching: [])
        .commands {
            CommandGroup(after: .newItem) {
                Button("Open Archive...") { openPanel() }
                    .keyboardShortcut("o")
            }
            CommandGroup(after: .pasteboard) {
                Divider()
                Button("Select All") {}
                    .keyboardShortcut("a")
            }
        }
    }

    private func openPanel() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.archive, .data]
        panel.allowsMultipleSelection = false
        panel.begin { result in
            guard result == .OK, let url = panel.url else { return }
            Task { try? await service.openArchive(at: url.path) }
        }
    }
}
