#!/usr/bin/env bash
set -euo pipefail

CONFIGURATION="${1:-Release}"
COMPILER="${2:-Clang}"
PHYSX_INPUT="${3:-}"

case "$CONFIGURATION" in
    Debug|Release) ;;
    *) echo "Configuration must be Debug or Release." >&2; exit 2 ;;
esac

case "$COMPILER" in
    Clang) export CC=clang CXX=clang++ ;;
    GCC) export CC=gcc CXX=g++ ;;
    *) echo "Compiler must be Clang or GCC." >&2; exit 2 ;;
esac

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
if [[ -z "$PHYSX_INPUT" ]]; then
    PHYSX_INPUT="$REPO_ROOT/../PhysX-110.1-omni-and-physx-5.9.0"
elif [[ "$PHYSX_INPUT" != /* ]]; then
    PHYSX_INPUT="$REPO_ROOT/$PHYSX_INPUT"
fi

if [[ -f "$PHYSX_INPUT/physx/CMakeLists.txt" ]]; then
    PHYSX_SOURCE="$PHYSX_INPUT/physx"
elif [[ -f "$PHYSX_INPUT/CMakeLists.txt" ]]; then
    PHYSX_SOURCE="$PHYSX_INPUT"
else
    echo "PhysX source was not found below: $PHYSX_INPUT" >&2
    exit 2
fi

# Keep generated PxConfig.h and all build output in the repository cache.
CACHE_ROOT="$REPO_ROOT/Build/.cache/PhysX-5.9.0"
CACHED_SOURCE="$CACHE_ROOT/physx-linux"
mkdir -p "$CACHED_SOURCE"
cp -a "$PHYSX_SOURCE/." "$CACHED_SOURCE/"

CONFIG_LOWER="${CONFIGURATION,,}"
COMPILER_LOWER="${COMPILER,,}"
BUILD_DIR="$CACHE_ROOT/build/linux-x64-$COMPILER_LOWER-$CONFIG_LOWER"
STAGE_DIR="$CACHE_ROOT/stage/linux-x64-$COMPILER_LOWER/$CONFIGURATION"

cmake -S "$CACHED_SOURCE" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$CONFIG_LOWER" \
    -DCMAKE_INSTALL_PREFIX="$STAGE_DIR" \
    -DPX_GENERATE_STATIC_LIBRARIES=ON \
    -DPX_GENERATE_GPU_PROJECTS=OFF \
    -DPX_GENERATE_GPU_STATIC_LIBRARIES=OFF \
    -DPX_BUILDSNIPPETS=OFF \
    -DPX_BUILDPVDRUNTIME=OFF \
    -DPX_USE_NVTX=OFF
cmake --build "$BUILD_DIR" --parallel
cmake --install "$BUILD_DIR"

if find "$STAGE_DIR/lib" "$STAGE_DIR/bin" -type f \( -iname '*PhysXGpu*' -o -iname '*cuda*' -o -iname '*.so' \) -print -quit 2>/dev/null | grep -q .; then
    echo "GPU/runtime artifacts were found in the CPU-only package." >&2
    exit 1
fi

PACKAGE_ROOT="$REPO_ROOT/OrbedenCore/Src/ThirdParty/PhysX"
PACKAGE_LIB="$PACKAGE_ROOT/lib/LinuxX64/$COMPILER/$CONFIGURATION"
mkdir -p "$PACKAGE_ROOT/include" "$PACKAGE_LIB"
find "$PACKAGE_LIB" -maxdepth 1 -type f -name '*.a' -delete
cp -a "$STAGE_DIR/include/." "$PACKAGE_ROOT/include/"
cp "$STAGE_DIR"/lib/*.a "$PACKAGE_LIB/"

echo "PhysX CPU-only $COMPILER $Configuration completed: $STAGE_DIR"
