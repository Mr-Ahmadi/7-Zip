#!/bin/bash
# Create 7-Zip.app bundle from SPM build output
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$PROJECT_DIR/.build/release}"
APP_NAME="7-Zip"
APP_BUNDLE="$BUILD_DIR/$APP_NAME.app"

echo "Creating $APP_BUNDLE..."
rm -rf "$APP_BUNDLE"

mkdir -p "$APP_BUNDLE/Contents/MacOS"
mkdir -p "$APP_BUNDLE/Contents/Resources"

# Copy executable
cp "$BUILD_DIR/$APP_NAME" "$APP_BUNDLE/Contents/MacOS/$APP_NAME"

# Copy icon
if [ -f "$PROJECT_DIR/Resources/AppIcon.icns" ]; then
    cp "$PROJECT_DIR/Resources/AppIcon.icns" "$APP_BUNDLE/Contents/Resources/AppIcon.icns"
fi

# Generate Info.plist
cat > "$APP_BUNDLE/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>$APP_NAME</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleIdentifier</key>
    <string>com.sevenzip.macos</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>7-Zip</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSHumanReadableCopyright</key>
    <string>7-Zip macOS</string>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>Archive</string>
            <key>CFBundleTypeRole</key>
            <string>Editor</string>
            <key>LSHandlerRank</key>
            <string>Default</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>org.7-zip.7-zip-archive</string>
                <string>com.pkware.zip-archive</string>
                <string>com.rarlab.rar-archive</string>
                <string>public.tar-archive</string>
                <string>org.gnu.gnu-zip-archive</string>
                <string>public.bzip2-archive</string>
                <string>public.xz-archive</string>
                <string>public.iso-image</string>
            </array>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>7z</string>
                <string>zip</string>
                <string>rar</string>
                <string>tar</string>
                <string>tgz</string>
                <string>tbz2</string>
                <string>txz</string>
                <string>gz</string>
                <string>bz2</string>
                <string>xz</string>
                <string>iso</string>
                <string>cab</string>
                <string>arj</string>
                <string>lzh</string>
                <string>lha</string>
                <string>z</string>
                <string>wim</string>
                <string>swm</string>
                <string>dmg</string>
                <string>hfs</string>
                <string>vhd</string>
                <string>vmdk</string>
            </array>
            <key>CFBundleTypeIconFile</key>
            <string>AppIcon</string>
        </dict>
    </array>
</dict>
</plist>
EOF

echo "Done: $APP_BUNDLE"
ls -R "$APP_BUNDLE"
