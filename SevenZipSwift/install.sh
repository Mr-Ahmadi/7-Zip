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

info()  { echo -e "${GREEN}==>${NC} ${BOLD}$1${NC}"; }
warn()  { echo -e "${YELLOW}==>${NC} $1"; }
error() { echo -e "${RED}==>${NC} $1"; }

# ── Prerequisites ───────────────────────────────────────────────────

check_prereqs() {
    if ! command -v swift &>/dev/null; then
        error "Swift is not installed. Install Xcode or Command Line Tools:"
        echo "  xcode-select --install"
        exit 1
    fi
    local ver=$(swift --version | head -1 | grep -oE 'version [0-9]+\.[0-9]+' | grep -oE '[0-9]+\.[0-9]+' || echo "0")
    local maj=${ver%.*}
    local min=${ver#*.}
    if [ "$maj" -lt 5 ] || { [ "$maj" -eq 5 ] && [ "$min" -lt 9 ]; }; then
        error "Swift 5.9+ required (found $ver). Update Xcode."
        exit 1
    fi

    # Check for archiving backend
    if command -v 7za &>/dev/null; then
        info "Archive tool: 7za ($(command -v 7za))"
    elif command -v 7z &>/dev/null; then
        info "Archive tool: 7z ($(command -v 7z))"
    else
        warn "No 7z/7za found on PATH. Install p7zip for archiving:"
        echo "  brew install p7zip"
    fi
}

# ── Install CLI ─────────────────────────────────────────────────────

install_cli() {
    info "Building 7z CLI (release)..."
    swift build --product 7z --configuration release

    info "Installing 7z to /usr/local/bin/..."
    mkdir -p /usr/local/bin
    cp -f "$PROJECT_DIR/.build/release/7z" /usr/local/bin/7z
    chmod +x /usr/local/bin/7z
    echo "  ✓ /usr/local/bin/7z"
}

# ── Install GUI ─────────────────────────────────────────────────────

install_gui() {
    info "Building 7-Zip GUI (release)..."
    swift build --product "7-Zip" --configuration release

    info "Creating 7-Zip.app bundle..."
    bash "$PROJECT_DIR/scripts/make-app-bundle.sh"

    info "Installing 7-Zip.app to /Applications/..."
    local app_src="$PROJECT_DIR/.build/release/7-Zip.app"
    if [ -d "$app_src" ]; then
        rm -rf /Applications/7-Zip.app 2>/dev/null || true
        cp -Rf "$app_src" /Applications/7-Zip.app
        echo "  ✓ /Applications/7-Zip.app"

        # Register with Launch Services so Finder picks up the icon
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
    [ -f /usr/local/bin/7z ] && rm -f /usr/local/bin/7z && echo "  ✓ /usr/local/bin/7z" && n=1
    [ -d /Applications/7-Zip.app ] && rm -rf /Applications/7-Zip.app && echo "  ✓ /Applications/7-Zip.app" && n=1
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
            exec sudo "$0" uninstall "$@"
        fi
        uninstall
        ;;
    *)
        echo "Usage: $0 [install|uninstall]"
        ;;
esac
