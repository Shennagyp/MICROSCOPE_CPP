#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${AMSCOPE_SDK_PROBE_BUILD_DIR:-${repo_root}/build/amscope-sdk-probe}"

case "$(uname -m)" in
    arm64|aarch64)
        target_arch="arm64"
        vendor_arch="x64+arm64"
        ;;
    x86_64)
        target_arch="x86_64"
        vendor_arch="x64+x86"
        ;;
    *)
        echo "Unsupported macOS host architecture: $(uname -m)" >&2
        exit 2
        ;;
esac

cmake -S "${repo_root}/Camera/Amscope/tools/pixel_format_probe" \
    -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="${target_arch}" \
    -DAMSCOPE_VENDOR_MAC_ARCH="${vendor_arch}"

cmake --build "${build_dir}" --config Release --parallel

exec "${build_dir}/amscope_sdk_pixel_format_probe" "$@"
