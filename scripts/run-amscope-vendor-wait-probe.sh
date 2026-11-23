#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/amscope-vendor-wait-probe"
BUILD_TYPE="${AMSCOPE_BUILD_TYPE:-Release}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "The bundled Amcam native SDK target currently supports macOS only." >&2
    exit 1
fi

if [[ -n "${CMAKE_EXECUTABLE:-}" ]]; then
    CMAKE_BIN="${CMAKE_EXECUTABLE}"
elif command -v cmake >/dev/null 2>&1; then
    CMAKE_BIN="$(command -v cmake)"
elif [[ -x "/Applications/CMake.app/Contents/bin/cmake" ]]; then
    CMAKE_BIN="/Applications/CMake.app/Contents/bin/cmake"
elif [[ -x "${HOME}/Applications/CMake.app/Contents/bin/cmake" ]]; then
    CMAKE_BIN="${HOME}/Applications/CMake.app/Contents/bin/cmake"
else
    echo "CMake not found. Install CMake.app or set CMAKE_EXECUTABLE." >&2
    exit 1
fi

CMAKE_ARGS=(
    -S "${REPO_ROOT}/Camera/Amscope"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DAMSCOPE_VENDOR_MAC_ARCH="${AMSCOPE_VENDOR_MAC_ARCH:-auto}"
    -DAMSCOPE_BUILD_NATIVE_PROBE=OFF
    -DAMSCOPE_BUILD_SDK_WAIT_PROBE=ON
    -DAMSCOPE_BUILD_QT_ADAPTER=OFF
    -DAMSCOPE_BUILD_PREVIEW_APP=OFF
)

if [[ -n "${CMAKE_OSX_ARCHITECTURES:-}" ]]; then
    CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
else
    case "$(uname -m)" in
        arm64|aarch64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64") ;;
        x86_64)        CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
        *)
            echo "Unsupported Mac architecture: $(uname -m)" >&2
            exit 1
            ;;
    esac
fi

"${CMAKE_BIN}" "${CMAKE_ARGS[@]}"
"${CMAKE_BIN}" --build "${BUILD_DIR}" --target amscope_sdk_wait_probe \
    --config "${BUILD_TYPE}" --parallel

exec "${BUILD_DIR}/amscope_sdk_wait_probe" "$@"
