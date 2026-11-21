#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/amscope"
BUILD_TYPE="${AMSCOPE_BUILD_TYPE:-Release}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "The bundled Amcam native SDK target currently supports macOS only." >&2
    exit 1
fi

# Reuse the caller's Qt installation when supplied; otherwise discover the
# Homebrew Qt 6 installation on the current Mac.
if [[ -z "${CMAKE_PREFIX_PATH:-}" ]]; then
    if ! command -v brew >/dev/null 2>&1; then
        echo "Qt 6 not found. Set CMAKE_PREFIX_PATH to your Qt 6 installation." >&2
        exit 1
    fi

    QT_PREFIX="$(brew --prefix qt@6 2>/dev/null || brew --prefix qt 2>/dev/null || true)"
    if [[ -z "${QT_PREFIX}" ]]; then
        echo "Could not locate Qt 6. Install it with 'brew install qt@6' or set CMAKE_PREFIX_PATH." >&2
        exit 1
    fi
    export CMAKE_PREFIX_PATH="${QT_PREFIX}"
fi

CMAKE_ARGS=(
    -S "${REPO_ROOT}/Camera/Amscope"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DAMSCOPE_VENDOR_MAC_ARCH="${AMSCOPE_VENDOR_MAC_ARCH:-auto}"
    -DAMSCOPE_BUILD_QT_ADAPTER=ON
    -DAMSCOPE_BUILD_PREVIEW_APP=ON
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

cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --parallel

# Pass any additional arguments through to the preview application.
exec "${BUILD_DIR}/amscope_preview" "$@"
