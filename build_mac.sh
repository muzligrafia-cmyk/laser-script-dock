#!/bin/bash
# ============================================================================
#  Laser Script Dock - build for Mac
#  Needs: Xcode (from the App Store, opened once), CMake (cmake.org or
#  `brew install cmake`) and the Illustrator SDK for Mac.
#
#  Run in Terminal:   ./build_mac.sh
#  If the SDK isn't found:   AI_SDK_ROOT="/path/to/SDK" ./build_mac.sh
# ============================================================================
set -e
cd "$(dirname "$0")"

SDK="${AI_SDK_ROOT:-}"
if [ -z "$SDK" ]; then
    for d in "$HOME"/SDK/* "$HOME"/Documents/*SDK* "$HOME"/Downloads/*SDK* "$HOME"/Desktop/*SDK* /Users/Shared/SDK/*; do
        [ -d "$d/illustratorapi" ] && SDK="$d"
    done
fi
if [ -z "$SDK" ] || [ ! -d "$SDK/illustratorapi" ]; then
    echo "[X] Couldn't find the Illustrator SDK. Run it like this:"
    echo "    AI_SDK_ROOT=\"/path/to/the/SDK\" ./build_mac.sh"
    exit 1
fi
command -v cmake >/dev/null || { echo "[X] CMake not found. Install it: brew install cmake  (or from cmake.org)"; exit 1; }

echo "SDK: $SDK"
echo "Building... (the first time takes a minute or two)"

cmake -S . -B build-mac -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DAI_SDK_ROOT="$SDK" > build-mac-log.txt 2>&1 \
    || { echo "[X] Setup failed - see build-mac-log.txt"; exit 1; }
cmake --build build-mac --config Release >> build-mac-log.txt 2>&1 \
    || { echo "[X] Compiling failed. Important lines:"; grep -E "error:" build-mac-log.txt | head -20; echo "Full details: build-mac-log.txt"; exit 1; }

rm -rf dist-mac
mkdir -p dist-mac
cp -R build-mac/Release/LaserScriptDock.aip dist-mac/
codesign --force --deep -s - dist-mac/LaserScriptDock.aip >/dev/null 2>&1 || true

echo
echo "[OK] Built: dist-mac/LaserScriptDock.aip"
echo
echo "To install: quit Illustrator, then copy LaserScriptDock.aip into"
echo "  /Applications/Adobe Illustrator 2027/Plug-ins.localized/"
echo "If the plugin came from another Mac, run this once after copying:"
echo "  xattr -dr com.apple.quarantine \"/Applications/Adobe Illustrator 2027/Plug-ins.localized/LaserScriptDock.aip\""
