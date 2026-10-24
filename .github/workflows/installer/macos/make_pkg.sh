#!/bin/bash
# Builds the macOS installer (.pkg) from the compiled plugin bundles.
set -euo pipefail
cd "$(dirname "$0")/../.."
ART="build/SketchDelay_artefacts/Release"
STAGE="build/pkgroot"
rm -rf "$STAGE"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Library/Audio/Plug-Ins/Components" dist
cp -R "$ART/VST3/Sketch Delay.vst3"    "$STAGE/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/Sketch Delay.component" "$STAGE/Library/Audio/Plug-Ins/Components/"
codesign --force --deep -s - "$STAGE/Library/Audio/Plug-Ins/VST3/Sketch Delay.vst3"
codesign --force --deep -s - "$STAGE/Library/Audio/Plug-Ins/Components/Sketch Delay.component"
pkgbuild --root "$STAGE" --identifier com.techhousetools.sketchdelay.pkg --version 1.0.0 --install-location / dist/SketchDelay-macOS.pkg
echo "Created dist/SketchDelay-macOS.pkg"
