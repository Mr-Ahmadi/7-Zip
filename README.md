# 7-Zip for macOS

A native macOS archive manager with both **GUI** and **CLI** interfaces.

## Features

- **SwiftUI GUI** – modern macOS app with archive browsing, extraction, and creation
- **CLI + TUI** – command-line tool with terminal UI mode (`7z list`, `7z extract`, `7z create`)
- **Formats** – 7z, ZIP, RAR (extract), TAR, GZ, BZ2, XZ, ISO and more
- **File associations** – right-click any archive → "Open With" → 7-Zip
- **Drag & drop** – drop archives onto the app window

## Quick Start

### Prerequisites

- macOS 14+
- Xcode 15+ (or Command Line Tools)
- [p7zip](https://p7zip.sourceforge.net/) (`7za` binary must be on PATH)

### Build & Install (SwiftUI App)

```bash
cd SevenZipSwift

# Build GUI app
swift build --product "7-Zip" --configuration release

# Build CLI tool
swift build --product "7z" --configuration release

# Install to /Applications
./install.sh
```

### Build (Qt/C++ App)

```bash
mkdir build && cd build
cmake ..
make
```

## Project Structure

```
7-Zip/
├── SevenZipSwift/         # Swift/SwiftUI implementation
│   ├── Sources/
│   │   ├── SevenZipApp/   #   macOS GUI app (SwiftUI)
│   │   ├── SevenZipCLI/   #   CLI + TUI tool
│   │   ├── CSevenZip/     #   C++ archive bridge
│   │   └── CNcurses/      #   NCurses wrapper
│   ├── Resources/         # Icons, asset catalog
│   └── install.sh         # Install/uninstall script
├── src/                   # Qt/C++ implementation
├── resources/             # Qt app resources
└── CMakeLists.txt         # Qt build config
```

## Usage

**GUI:** Launch from Applications or open archives via right-click → "Open With".

**CLI:**
```bash
# List archive contents
7z list archive.7z

# Extract archive
7z extract archive.7z

# Create archive
7z create output.7z file1.txt file2.txt

# Interactive terminal UI
tui archive.7z
```

## License

This project is based on p7zip and the 7-Zip SDK.
