#!/usr/bin/env bash
# Install breakbeat module to Move.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

MODULE_ID="breakbeat"
TARGET="/data/UserData/schwung/modules/sound_generators/${MODULE_ID}"

if [ ! -d "dist/${MODULE_ID}" ]; then
    echo "Error: dist/${MODULE_ID} not found. Run ./scripts/build.sh first."
    exit 1
fi

PORTAL="/data/UserData/breakbeat-samples"

echo "=== Installing breakbeat module ==="
ssh ableton@move.local "mkdir -p ${TARGET}"
scp -r "dist/${MODULE_ID}"/* "ableton@move.local:${TARGET}/"
ssh ableton@move.local "chmod -R a+rw ${TARGET}"

# Build a "portal" directory that the filepath browser uses as its root.
# Symlinks expose the bundled samples and the user's sample library side-by-side.
ssh ableton@move.local "
mkdir -p '${PORTAL}'
ln -sfn '${TARGET}/samples' '${PORTAL}/Built-in'
if [ -d /data/UserData/UserLibrary/Samples ]; then
    ln -sfn /data/UserData/UserLibrary/Samples '${PORTAL}/User Library'
fi
"

echo ""
echo "Installed to: ${TARGET}"
echo "Browser portal: ${PORTAL}"
echo "Restart Schwung to load the new module."
