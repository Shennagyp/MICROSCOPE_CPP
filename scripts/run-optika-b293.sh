#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/optika-b293-ninja"
BUILD_TYPE="${OPTIKA_BUILD_TYPE:-Release}"
APP_BUNDLE="${BUILD_DIR}/Application/OptikaB293/optika_b293_dualslide_app.app"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "OPTIKA B293 is a native macOS application." >&2
    exit 1
fi

CMAKE_COMMAND="${CMAKE_EXECUTABLE:-}"
if [[ -n "${CMAKE_COMMAND}" ]]; then
    if [[ ! -x "${CMAKE_COMMAND}" ]]; then
        echo "CMAKE_EXECUTABLE is not executable: ${CMAKE_COMMAND}" >&2
        exit 1
    fi
elif command -v cmake >/dev/null 2>&1; then
    CMAKE_COMMAND="$(command -v cmake)"
else
    echo "CMake was not found. Install CMake or set CMAKE_EXECUTABLE." >&2
    exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "Ninja was not found. Install it with 'brew install ninja'." >&2
    exit 1
fi

"${CMAKE_COMMAND}" \
    -S "${REPO_ROOT}" \
    -B "${BUILD_DIR}" \
    -G Ninja \
    -DOPTIKA_BUILD_NATIVE_APP=ON \
    -DAMSCOPE_BUILD_QT_ADAPTER=OFF \
    -DAMSCOPE_BUILD_PREVIEW_APP=OFF \
    -DBUILD_TESTING=ON \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

"${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target optika_b293_dualslide_app --parallel

if [[ ! -d "${APP_BUNDLE}" ]]; then
    echo "Build completed but the expected app bundle was not created:" >&2
    echo "  ${APP_BUNDLE}" >&2
    exit 1
fi

exec /usr/bin/open "${APP_BUNDLE}"
