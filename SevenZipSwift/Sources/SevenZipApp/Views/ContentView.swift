import SwiftUI

struct ContentView: View {
    @EnvironmentObject var service: ArchiveService
    @State private var showDropTarget = false
    @State private var password = ""
    @State private var passwordError: String?
    @State private var showPasswordPrompt = false

    var body: some View {
        ZStack {
            if service.isArchiveOpen {
                ArchiveBrowserView()
                    .transition(.opacity)
            } else {
                HomeView()
                    .transition(.opacity)
            }

            if case .loading = service.action {
                loadingOverlay
            }
        }
        .animation(.easeInOut(duration: 0.2), value: service.isArchiveOpen)
        .animation(.easeInOut(duration: 0.2), value: service.action)
        .onDrop(of: [.fileURL], isTargeted: $showDropTarget) { providers in
            handleDrop(providers)
            return true
        }
        .overlay {
            if showDropTarget {
                dropOverlay
            }
        }
        .onAppear {
            openPending()
        }
        .onChange(of: service.pendingPath) {
            openPending()
        }
        .onChange(of: service.requiresPassword) {
            if service.requiresPassword {
                password = ""
                passwordError = nil
                showPasswordPrompt = true
            }
        }
        .sheet(isPresented: $showPasswordPrompt, onDismiss: {
            if service.requiresPassword {
                service.cancelPasswordPrompt()
            }
        }) {
            passwordPromptView
        }
    }

    private func openPending() {
        guard let path = service.pendingPath else { return }
        service.pendingPath = nil
        Task { try? await service.openArchive(at: path) }
    }

    // MARK: - Password Prompt

    private var passwordPromptView: some View {
        VStack(spacing: 16) {
            Image(systemName: "lock.fill")
                .font(.system(size: 32))
                .foregroundStyle(.tint)

            Text("Encrypted Archive")
                .font(.headline)

            if passwordError != nil {
                Text("Wrong password. Try again.")
                    .font(.subheadline)
                    .foregroundStyle(.red)
            } else {
                Text("This archive is password-protected.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }

            SecureField("Password", text: $password)
                .textFieldStyle(.roundedBorder)
                .frame(width: 220)
                .onChange(of: password) { passwordError = nil }

            HStack(spacing: 12) {
                Button("Cancel") {
                    service.cancelPasswordPrompt()
                    showPasswordPrompt = false
                }
                .keyboardShortcut(.escape)

                Button("Open") {
                    let pwd = password
                    Task {
                        if let err = await service.retryWithPassword(pwd) {
                            passwordError = err
                            password = ""
                        } else {
                            showPasswordPrompt = false
                        }
                    }
                }
                .keyboardShortcut(.return)
                .disabled(password.isEmpty)
                .buttonStyle(.borderedProminent)
            }
        }
        .padding(24)
        .frame(width: 300)
    }

    // MARK: - Loading

    private var loadingOverlay: some View {
        ZStack {
            Color.black.opacity(0.15)
                .ignoresSafeArea()

            ProgressView("Loading archive...")
                .padding(24)
                .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 12))
        }
    }

    // MARK: - Drop

    private var dropOverlay: some View {
        ZStack {
            Color.accentColor.opacity(0.08)
                .ignoresSafeArea()

            RoundedRectangle(cornerRadius: 20)
                .stroke(.tint, style: SwiftUI.StrokeStyle(lineWidth: 2, dash: [12, 8]))
                .padding(20)

            VStack(spacing: 12) {
                Image(systemName: "arrow.down.doc.fill")
                    .font(.system(size: 40))
                    .foregroundStyle(.tint)
                Text("Drop Archive to Open")
                    .font(.title2.weight(.semibold))
                Text("7z  ·  zip  ·  rar  ·  tar  ·  gz  ·  bz2  ·  xz")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
        }
        .ignoresSafeArea()
        .transition(.opacity.animation(.easeInOut(duration: 0.15)))
    }

    private func handleDrop(_ providers: [NSItemProvider]) {
        guard let provider = providers.first else { return }
        provider.loadItem(forTypeIdentifier: "public.file-url", options: nil) { item, _ in
            guard let data = item as? Data,
                  let url = URL(dataRepresentation: data, relativeTo: nil) else { return }
            DispatchQueue.main.async {
                Task { try? await service.openArchive(at: url.path) }
            }
        }
    }
}
