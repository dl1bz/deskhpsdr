#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFTW_VERSION="3.3.11"
FFTW_ARCHIVE="fftw-${FFTW_VERSION}.tar.gz"
FFTW_URL="https://www.fftw.org/${FFTW_ARCHIVE}"

SOURCE_DIR="${SCRIPT_DIR}/fftw-3.3.11"

if [[ ! -d "${SOURCE_DIR}" ]]; then
    ARCHIVE_PATH="${SCRIPT_DIR}/${FFTW_ARCHIVE}"

    if [[ ! -f "${ARCHIVE_PATH}" ]]; then
        echo "Downloading FFTW ${FFTW_VERSION}..."

        if command -v curl >/dev/null 2>&1; then
            curl --fail --location --output "${ARCHIVE_PATH}" "${FFTW_URL}"
        elif command -v wget >/dev/null 2>&1; then
            wget --output-document="${ARCHIVE_PATH}" "${FFTW_URL}"
        else
            echo "Error: neither curl nor wget is available." >&2
            exit 1
        fi
    else
        echo "Using existing archive: ${ARCHIVE_PATH}"
    fi

    echo "Extracting FFTW ${FFTW_VERSION}..."
    tar -xzf "${ARCHIVE_PATH}" -C "${SCRIPT_DIR}"
fi

DOUBLE_BUILD_DIR="${SOURCE_DIR}/_build-double"
FLOAT_BUILD_DIR="${SOURCE_DIR}/_build-float"

DOUBLE_PREFIX="${SOURCE_DIR}/build"
FLOAT_PREFIX="${SOURCE_DIR}/build-float"

if [[ ! -x "${SOURCE_DIR}/configure" ]]; then
    echo "Error: FFTW source tree is incomplete." >&2
    echo "Missing executable: ${SOURCE_DIR}/configure" >&2
    exit 1
fi

if [[ -f "${SOURCE_DIR}/config.status" ]]; then
    echo "Removing previous in-source FFTW configuration..."

    if [[ -f "${SOURCE_DIR}/Makefile" ]]; then
        make -C "${SOURCE_DIR}" distclean
    fi
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

COMMON_CONFIGURE_FLAGS=(
    --disable-shared
    --enable-static
    --disable-fortran
    --with-pic
)

PLATFORM_CONFIGURE_FLAGS=()
PLATFORM_CFLAGS="-O3"
PLATFORM_CC=""

case "$(uname -s)-$(uname -m)" in
    Darwin-arm64)
        echo "FFTW platform optimization: macOS ARM64 / NEON"
        PLATFORM_CONFIGURE_FLAGS+=(--enable-neon)
        PLATFORM_CFLAGS="-O3 -mcpu=native"
        PLATFORM_CC="clang"
        ;;
    Linux-aarch64|Linux-arm64)
        echo "FFTW platform optimization: Linux ARM64 / NEON"
        PLATFORM_CONFIGURE_FLAGS+=(--enable-neon)
        PLATFORM_CFLAGS="-O3 -mcpu=native"
        ;;
    Linux-x86_64)
        echo "FFTW platform optimization: Linux x86_64 / SSE2 / AVX / AVX2"
        PLATFORM_CONFIGURE_FLAGS+=(--enable-sse2 --enable-avx --enable-avx2)
        PLATFORM_CFLAGS="-O3 -march=native"
        ;;
    Darwin-x86_64)
        echo "FFTW platform optimization: macOS x86_64 / SSE2 / AVX / AVX2"
        PLATFORM_CONFIGURE_FLAGS+=(--enable-sse2 --enable-avx --enable-avx2)
        PLATFORM_CFLAGS="-O3 -march=native"
        PLATFORM_CC="clang"
        ;;
    *)
        echo "FFTW platform optimization: generic"
        ;;
esac

echo "Removing previous FFTW build and installation directories..."
rm -rf "${DOUBLE_BUILD_DIR}"
rm -rf "${FLOAT_BUILD_DIR}"
rm -rf "${DOUBLE_PREFIX}"
rm -rf "${FLOAT_PREFIX}"

mkdir -p "${DOUBLE_BUILD_DIR}"
mkdir -p "${FLOAT_BUILD_DIR}"

echo ""
echo "Building FFTW 3.3.11 double precision..."
cd "${DOUBLE_BUILD_DIR}"
CC="${PLATFORM_CC:-cc}" CFLAGS="${PLATFORM_CFLAGS}" \
"${SOURCE_DIR}/configure" "${COMMON_CONFIGURE_FLAGS[@]}" \
    "${PLATFORM_CONFIGURE_FLAGS[@]}" --prefix="${DOUBLE_PREFIX}"
make -j"${JOBS}"
make install

echo ""
echo "Building FFTW 3.3.11 single precision..."
cd "${FLOAT_BUILD_DIR}"
CC="${PLATFORM_CC:-cc}" CFLAGS="${PLATFORM_CFLAGS}" \
"${SOURCE_DIR}/configure" "${COMMON_CONFIGURE_FLAGS[@]}" \
    "${PLATFORM_CONFIGURE_FLAGS[@]}" --enable-float --prefix="${FLOAT_PREFIX}"
make -j"${JOBS}"
make install

DOUBLE_HEADER="${DOUBLE_PREFIX}/include/fftw3.h"
DOUBLE_LIBRARY="${DOUBLE_PREFIX}/lib/libfftw3.a"
FLOAT_LIBRARY="${FLOAT_PREFIX}/lib/libfftw3f.a"

echo ""
echo "Verifying FFTW installation..."

for file in "${DOUBLE_HEADER}" "${DOUBLE_LIBRARY}" "${FLOAT_LIBRARY}"; do
    if [[ ! -f "${file}" ]]; then
        echo "Error: expected file was not created: ${file}" >&2
        exit 1
    fi
done

echo "Double header:  ${DOUBLE_HEADER}"
echo "Double library: ${DOUBLE_LIBRARY}"
echo "Float library:  ${FLOAT_LIBRARY}"

if [[ "$(uname -s)" == "Darwin" ]] && command -v lipo >/dev/null 2>&1; then
    echo ""
    lipo -info "${DOUBLE_LIBRARY}"
    lipo -info "${FLOAT_LIBRARY}"
fi

echo ""
echo "FFTW 3.3.11 build completed successfully."
