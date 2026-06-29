#!/usr/bin/env bash
#
# dev_build.sh — build the N64FlashcartMenu ROM in a Docker container without
# needing a local libdragon toolchain or VSCode dev container.
#
# Prerequisites:
#   - Docker installed and running.
#   - The `n64menu-dev` image built (see docs/93_local_build_and_emulator.md).
#     Quick build of the image:
#       docker build -t n64menu-build -f .devcontainer/flashcart/Dockerfile.sc64deployer .devcontainer/flashcart
#       docker build -t n64menu-dev   -f .devcontainer/flashcart/Dockerfile.dev          .
#
# Usage:
#   ./dev_build.sh              # normal build (FLAGS=-DNDEBUG), outputs to output/
#   ./dev_build.sh clean        # make clean
#   FLAGS="-DFEATURE_AUTOLOAD_ROM_ENABLED" ./dev_build.sh
#
set -euo pipefail

IMAGE="${IMAGE:-n64menu-dev}"
FLAGS="${FLAGS:--DNDEBUG}"
TARGET="${1:-all}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "error: docker image '$IMAGE' not found." >&2
    echo "Build it first (see docs/93_local_build_and_emulator.md)." >&2
    exit 1
fi

docker run --rm \
    -v "$REPO_ROOT":/work -w /work \
    -e N64_INST=/opt/libdragon \
    -e FLAGS="$FLAGS" \
    "$IMAGE" \
    make "$TARGET" -j

echo
echo "Build complete. ROMs in: $REPO_ROOT/output/"
ls -la "$REPO_ROOT/output/" 2>/dev/null || true
