#!/bin/bash
# Packages the built plugin bundles into a .pkg installer and a .zip for macOS.
#   packaging/package_macos.sh <artefacts dir> <output dir> <version>
set -euo pipefail

ARTEFACTS="$1"
OUT="$2"
VERSION="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
NAME="ToneShaper-${VERSION}-macOS"

rm -rf "$OUT"
mkdir -p "$OUT"
WORK="$(mktemp -d)"

AU="$ARTEFACTS/AU/ToneShaper.component"
VST3="$ARTEFACTS/VST3/ToneShaper.vst3"
APP="$ARTEFACTS/Standalone/ToneShaper.app"

# --- Installer package (installs system-wide; no quarantine on installed files) -----------
PAYLOAD="$WORK/payload"
mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/Components" "$PAYLOAD/Library/Audio/Plug-Ins/VST3" "$PAYLOAD/Applications"
ditto "$AU" "$PAYLOAD/Library/Audio/Plug-Ins/Components/ToneShaper.component"
ditto "$VST3" "$PAYLOAD/Library/Audio/Plug-Ins/VST3/ToneShaper.vst3"
ditto "$APP" "$PAYLOAD/Applications/ToneShaper.app"

# Never let Installer "relocate" bundles to wherever an older copy happens to live.
pkgbuild --analyze --root "$PAYLOAD" "$WORK/components.plist"
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/components.plist" > /dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/components.plist"
    i=$((i + 1))
done

mkdir -p "$WORK/scripts"
cp "$HERE/postinstall" "$WORK/scripts/postinstall"
chmod +x "$WORK/scripts/postinstall"

pkgbuild --root "$PAYLOAD" \
         --component-plist "$WORK/components.plist" \
         --scripts "$WORK/scripts" \
         --identifier com.arenttodal.toneshaper.pkg \
         --version "$VERSION" \
         --install-location / \
         "$WORK/ToneShaper-component.pkg"

productbuild --package "$WORK/ToneShaper-component.pkg" "$OUT/$NAME.pkg"

# --- Zip with the raw bundles and a user-level install script ------------------------------
ZIPDIR="$WORK/$NAME"
mkdir -p "$ZIPDIR"
ditto "$AU" "$ZIPDIR/ToneShaper.component"
ditto "$VST3" "$ZIPDIR/ToneShaper.vst3"
ditto "$APP" "$ZIPDIR/ToneShaper.app"
cp "$HERE/Install ToneShaper.command" "$ZIPDIR/"
chmod +x "$ZIPDIR/Install ToneShaper.command"
cp "$HERE/README-macOS.txt" "$ZIPDIR/README.txt"
(cd "$WORK" && ditto -c -k --sequesterRsrc --keepParent "$NAME" "$OUT/$NAME.zip")

(cd "$OUT" && shasum -a 256 *.pkg *.zip > SHA256SUMS.txt)
rm -rf "$WORK"
ls -la "$OUT"
