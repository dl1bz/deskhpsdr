#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

LWS_URL="https://libwebsockets.org/repo/libwebsockets"

SOURCE_DIR="${SCRIPT_DIR}/libwebsockets-5"
BUILD_DIR="${SOURCE_DIR}/_build"
PREFIX="${SOURCE_DIR}/build"

if [[ ! -d "${SOURCE_DIR}/.git" ]]; then
    echo "Cloning libwebsockets..."

    if ! command -v git >/dev/null 2>&1; then
        echo "Error: git isn't available." >&2
        exit 1
    fi

    rm -rf "${SOURCE_DIR}"
    git clone "${LWS_URL}" "${SOURCE_DIR}"
else
    echo "Using existing libwebsockets source: ${SOURCE_DIR}"
fi

case "$(uname -s)" in
    Darwin)
        JOBS="$(sysctl -n hw.logicalcpu)"
        ;;
    Linux)
        JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc)"
        ;;
    *)
        echo "Error: unsupported operating system: $(uname -s)" >&2
        exit 1
        ;;
esac

echo "Removing previous libwebsockets build and installation directories..."
rm -rf "${BUILD_DIR}"
rm -rf "${PREFIX}"

cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DLWS_WITH_STATIC=ON \
    -DLWS_WITH_SHARED=OFF \
    -DLWS_WITH_SSL=ON \
    -DLWS_ROLE_QUIC=OFF \
    -DLWS_WITH_HTTP3=OFF \
    -DLWS_WITHOUT_TESTAPPS=ON \
    -DLWS_WITHOUT_TEST_SERVER=ON \
    -DLWS_WITHOUT_TEST_CLIENT=ON \
    -DLWS_WITHOUT_TEST_PING=ON

cmake --build "${BUILD_DIR}" -j"${JOBS}"
cmake --install "${BUILD_DIR}"

HEADER="${PREFIX}/include/libwebsockets.h"
LIBRARY="${PREFIX}/lib/libwebsockets.a"

echo ""
echo "Verifying libwebsockets installation..."

for file in "${HEADER}" "${LIBRARY}"; do
    if [[ ! -f "${file}" ]]; then
        echo "Error: expected file was not created: ${file}" >&2
        exit 1
    fi
done

echo "Header:  ${HEADER}"
echo "Library: ${LIBRARY}"

if [[ "$(uname -s)" == "Darwin" ]] && command -v lipo >/dev/null 2>&1; then
    echo ""
    lipo -info "${LIBRARY}"
fi

echo ""
echo "libwebsockets build completed successfully."

