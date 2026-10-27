#!/bin/bash
# Installs ToneShaper for the current user (no administrator password needed).
# Double-click this file in Finder. If macOS refuses to open it, right-click it and choose Open
# (or allow it under System Settings > Privacy & Security).
set -e
cd "$(dirname "$0")"

COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
APPS="$HOME/Applications"
mkdir -p "$COMPONENTS" "$VST3" "$APPS"

echo "Installing ToneShaper..."
rm -rf "$COMPONENTS/ToneShaper.component" "$VST3/ToneShaper.vst3" "$APPS/ToneShaper.app"
ditto ToneShaper.component "$COMPONENTS/ToneShaper.component"
ditto ToneShaper.vst3 "$VST3/ToneShaper.vst3"
ditto ToneShaper.app "$APPS/ToneShaper.app"

# The builds are not notarised: clear the download quarantine so hosts will load them.
xattr -dr com.apple.quarantine "$COMPONENTS/ToneShaper.component" "$VST3/ToneShaper.vst3" "$APPS/ToneShaper.app" 2>/dev/null || true

# Make Logic / GarageBand rescan Audio Units.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Done:"
echo "  AU          $COMPONENTS/ToneShaper.component"
echo "  VST3        $VST3/ToneShaper.vst3"
echo "  Standalone  $APPS/ToneShaper.app"
echo
echo "Restart your DAW (or rescan plug-ins) to see ToneShaper."
