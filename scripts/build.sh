#!/usr/bin/env bash
# Build breakbeat module for Move (aarch64).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

IMAGE_NAME="schwung-breakbeat-builder"
MODULE_ID="breakbeat"
DIST_DIR="dist/${MODULE_ID}"
TARBALL="dist/${MODULE_ID}-module.tar.gz"

# Verify module.json version matches the latest git tag so the tarball is
# never shipped with a stale version string. Only checked on the host (not
# inside Docker) since the container doesn't have access to git tags.
if [ ! -f "/.dockerenv" ]; then
    MODULE_VERSION=$(grep '"version"' src/module.json | head -1 | sed 's/.*"version": *"\([^"]*\)".*/\1/')
    GIT_TAG=$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')
    if [ -n "$GIT_TAG" ] && [ "$MODULE_VERSION" != "$GIT_TAG" ]; then
        echo "ERROR: module.json version ($MODULE_VERSION) does not match latest git tag ($GIT_TAG)."
        echo "       Bump the version in src/module.json and release.json before building a release."
        exit 1
    fi
fi

if [ -z "${CROSS_PREFIX:-}" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== breakbeat build (via Docker) ==="
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image..."
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        ./scripts/build.sh
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
CC="${CC:-${CROSS_PREFIX}gcc}"

rm -rf dist build
mkdir -p "$DIST_DIR" build

echo "Compiling DSP..."
"$CC" -Ofast -shared -fPIC \
    -march=armv8-a -mtune=cortex-a72 \
    -fomit-frame-pointer -fno-stack-protector \
    -DNDEBUG \
    -Isrc/dsp \
    src/dsp/breakbeat.c \
    src/dsp/slice_select.c \
    -o build/dsp.so \
    -lm

echo "Packaging..."
cat build/dsp.so > "$DIST_DIR/dsp.so"
chmod 0755 "$DIST_DIR/dsp.so"
cat src/module.json > "$DIST_DIR/module.json"
cat src/ui.js > "$DIST_DIR/ui.js"

# Bundle samples too (the module loads them at runtime from its install dir)
mkdir -p "$DIST_DIR/samples"
for f in samples/*.wav; do
    cat "$f" > "$DIST_DIR/samples/$(basename "$f")"
done

# Bundle presets too
mkdir -p "$DIST_DIR/presets"
for f in src/presets/*.json; do
    cat "$f" > "$DIST_DIR/presets/$(basename "$f")"
done

(cd dist && tar -czf "${MODULE_ID}-module.tar.gz" "${MODULE_ID}/")

echo "Built: $TARBALL"
ls -lh "$TARBALL"
file "$DIST_DIR/dsp.so"
