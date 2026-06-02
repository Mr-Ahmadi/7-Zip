import SwiftUI

struct HomeView: View {
    @EnvironmentObject var service: ArchiveService
    @State private var showFilePicker = false
    @State private var showCreateArchive = false

    var body: some View {
        VStack(spacing: 0) {
            Spacer()

            Image(systemName: "archivebox")
                .font(.system(size: 56))
                .foregroundStyle(.tint)
                .padding(.bottom, 12)

            Text("7-Zip")
                .font(.largeTitle.weight(.bold))

            Text("Open, extract, and create archives")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .padding(.bottom, 24)

            HStack(spacing: 16) {
                Button {
                    showFilePicker = true
                } label: {
                    Label("Browse Archives...", systemImage: "doc.badge.plus")
                        .frame(minWidth: 180)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)

                Button {
                    showCreateArchive = true
                } label: {
                    Label("New Archive...", systemImage: "plus.square")
                        .frame(minWidth: 140)
                }
                .buttonStyle(.bordered)
                .controlSize(.large)
            }

            if !service.recentArchives.isEmpty {
                VStack(spacing: 2) {
                    Text("RECENTLY OPENED")
                        .font(.caption2.weight(.semibold))
                        .foregroundStyle(.tertiary)
                        .padding(.top, 28)
                        .padding(.bottom, 8)

                    ForEach(service.recentArchives.prefix(6), id: \.self) { path in
                        Button {
                            Task { try? await service.openArchive(at: path) }
                        } label: {
                            HStack(spacing: 8) {
                                Image(systemName: "archivebox")
                                    .foregroundStyle(.tint)
                                    .font(.caption)
                                Text(path.fileName)
                                    .lineLimit(1)
                                    .foregroundStyle(.primary)
                                Spacer()
                                Text(path.fileNameWithoutExtension.fileExtension.uppercased())
                                    .font(.caption2.weight(.medium))
                                    .foregroundStyle(.tertiary)
                                    .monospacedDigit()
                            }
                            .padding(.horizontal, 12)
                            .padding(.vertical, 6)
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                        .background(Color.primary.opacity(0.03), in: RoundedRectangle(cornerRadius: 6))
                        .frame(maxWidth: 320)
                    }
                }
            }

            Spacer()

            if !service.isToolAvailable {
                HStack(spacing: 6) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundStyle(.orange)
                    Text("Archive engine not available")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding(.bottom, 20)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .fileImporter(
            isPresented: $showFilePicker,
            allowedContentTypes: [.archive, .data],
            allowsMultipleSelection: false
        ) { result in
            if case .success(let urls) = result, let url = urls.first {
                Task { try? await service.openArchive(at: url.path) }
            }
        }
        .sheet(isPresented: $showCreateArchive) {
            CreateArchiveView()
        }
        .background(
            Image(systemName: "archivebox")
                .font(.system(size: 300))
                .foregroundStyle(.quaternary.opacity(0.15))
                .rotationEffect(.degrees(-10))
                .offset(x: 140, y: 40)
        )
    }
}
