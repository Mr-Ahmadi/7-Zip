#!/bin/bash
set -euo pipefail

# ══════════════════════════════════════════════════════════════════════
# 7-Zip macOS Installer
# Installs the 7z CLI to /usr/local/bin and the GUI app to /Applications.
# ══════════════════════════════════════════════════════════════════════

BOLD='\033[1m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

info()  { printf "${GREEN}==>${NC} ${BOLD}%s${NC}\n" "$1"; }
warn()  { printf "${YELLOW}==>${NC} %s\n" "$1"; }
error() { printf "${RED}==>${NC} %s\n" "$1"; }

# Detect the SPM build output directory (includes target triple)
detect_build_dir() {
    local config="${1:-release}"
    local base="$PROJECT_DIR/.build"
    # Newer SwiftPM uses .build/<triple>/<config>/
    local dir
    dir=$(find "$base" -maxdepth 2 -type d -name "$config" 2>/dev/null | head -1)
    if [ -z "$dir" ] || [ ! -d "$dir" ]; then
        dir="$base/$config"
    fi
    echo "$dir"
}

# ── Prerequisites ───────────────────────────────────────────────────

check_prereqs() {
    if ! command -v swift &>/dev/null; then
        error "Swift is not installed. Install Xcode or Command Line Tools:"
        echo "  xcode-select --install"
        exit 1
    fi
    local ver
    ver="$(swift --version | grep -oE 'version [0-9]+\.[0-9]+' | tail -1 | grep -oE '[0-9]+\.[0-9]+')"
    if [ -z "$ver" ]; then
        error "Could not determine Swift version."
        exit 1
    fi
    if ! echo "$ver" | awk -F. '{ exit !($1 > 5 || ($1 == 5 && $2 >= 9)) }'; then
        error "Swift 5.9+ required (found $ver). Update Xcode."
        exit 1
    fi

    # Verify bundled archive tool exists
    if [ -f "$PROJECT_DIR/../resources/bin/7za" ]; then
        info "Archive tool: bundled 7za ($($PROJECT_DIR/../resources/bin/7za --help 2>&1 | head -1))"
    else
        warn "Bundled 7za binary not found at resources/bin/7za"
        warn "Compression will use system p7zip if available, or fail."
    fi
}

# ── Install CLI ─────────────────────────────────────────────────────

install_cli() {
    info "Building 7z CLI (release)..."
    swift build --product 7z -c release

    local build_dir
    build_dir="$(detect_build_dir "release")"
    info "Installing 7z to /usr/local/bin/..."
    mkdir -p /usr/local/bin
    cp -f "$build_dir/7z" /usr/local/bin/7z
    chmod +x /usr/local/bin/7z
    echo "  ✓ /usr/local/bin/7z"
}

# ── Install GUI ─────────────────────────────────────────────────────

install_gui() {
    info "Building 7-Zip GUI (release)..."
    swift build --product "7-Zip" -c release

    local build_dir
    build_dir="$(detect_build_dir "release")"
    info "Creating 7-Zip.app bundle..."
    bash "$PROJECT_DIR/scripts/make-app-bundle.sh" "$build_dir"

    info "Installing 7-Zip.app to /Applications/..."
    local app_src="$build_dir/7-Zip.app"
    if [ -d "$app_src" ]; then
        rm -rf /Applications/7-Zip.app 2>/dev/null || true
        cp -Rf "$app_src" /Applications/7-Zip.app
        echo "  ✓ /Applications/7-Zip.app"

        /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
            -f /Applications/7-Zip.app 2>/dev/null || true
    else
        warn "App bundle not found at $app_src"
    fi
}

# ── Uninstall ───────────────────────────────────────────────────────

uninstall() {
    info "Removing 7-Zip..."
    local n=0
    if [ -f /usr/local/bin/7z ]; then
        rm -f /usr/local/bin/7z && echo "  ✓ /usr/local/bin/7z" && n=$((n + 1))
    fi
    if [ -d /Applications/7-Zip.app ]; then
        rm -rf /Applications/7-Zip.app && echo "  ✓ /Applications/7-Zip.app" && n=$((n + 1))
    fi
    [ "$n" -eq 0 ] && echo "  Nothing to remove."
}

# ── Main ────────────────────────────────────────────────────────────

case "${1:-install}" in
    install)
        echo ""
        echo "╔══════════════════════════════════════════════════╗"
        echo "║        7-Zip macOS Installer                     ║"
        echo "╚══════════════════════════════════════════════════╝"
        echo ""

        cd "$PROJECT_DIR"

        if [ "$(id -u)" -ne 0 ]; then
            warn "Some operations require sudo (writing to /usr/local/bin, /Applications)."
            warn "Re-run with sudo if you see permission errors."
            echo ""
        fi

        check_prereqs
        echo ""
        install_cli
        echo ""
        install_gui
        echo ""
        info "Installation complete!"
        echo ""
        echo "  CLI:  7z --help"
        echo "  TUI:  7z"
        echo "  GUI:  open /Applications/7-Zip.app"
        echo ""
        echo "  Remove: sudo $0 uninstall"
        ;;
    uninstall)
        if [ "$(id -u)" -ne 0 ]; then
            exec sudo "$0" uninstall
        fi
        uninstall
        ;;
    *)
        echo "Usage: $0 [install|uninstall]"
        ;;
esac
