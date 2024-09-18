#!/bin/bash
set -e
WORK_DIR=$(dirname $(readlink -f "${BASH_SOURCE[0]}") )

if [ -z $1 ]; then
    mkdir -p install
    INSTALL_PATH=${WORK_DIR}/install
else
    INSTALL_PATH=$1
fi

if [ -z $2 ]; then
    FFMPEG_VERSION="4.4"
else
    FFMPEG_VERSION=$2
fi

MLU_CODEC=true
MLU_FILTER=true
LIBRARY_TYPE=shared
COMPILE_MODE=release
FFMPEG_MLU_SRC_FILE="ffmpeg-${FFMPEG_VERSION}.tar.gz"

# Check NEUWARE_HOME PATH
if [ -z ${NEUWARE_HOME} ]; then
    echo "Error: please setting NEUWARE_HOME environment variable"
    exit 1
fi

# Check library type
if [[ "$LIBRARY_TYPE" != "shared" && "$LIBRARY_TYPE" != "static" ]]; then
    echo "Unsupported library type: $LIBRARY_TYPE"
    exit 1
fi

# Check CPU architecture
ARCH=$(uname -m)
if [[ "$ARCH" != "x86_64" && "$ARCH" != "aarch64" && "$ARCH" != "loongarch64" ]]; then
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

# Check whether into Docker
if [ -f /.dockerenv ]; then
    IN_DOCKER=true
else
    IN_DOCKER=false
fi

# Prepare FFmpeg source code
extract_ffmpeg() {
    if [ -f $FFMPEG_MLU_SRC_FILE ]; then
        echo "Using local FFmpeg source file: $FFMPEG_MLU_SRC_FILE"
    else
        echo "Error: not fond local FFmpeg source file: $FFMPEG_MLU_SRC_FILE"
        exit 1
    fi
    tar -xf $FFMPEG_MLU_SRC_FILE
}

# Config、compile and install FFmpeg
compile_ffmpeg() {
    # Check MLU CODEC and MLU FILTER
    if [ "$MLU_CODEC" == "true" ]; then
        if [ "$MLU_FILTER" == "true" ]; then
            MLU_FLAGS="--enable-mlu --enable-mlumpp --enable-mlufilter"
        else
            MLU_FLAGS="--enable-mlu --enable-mlumpp"
        fi
    else
        MLU_FLAGS=""
    fi
    # Check lib mode
    if [ "$LIBRARY_TYPE" == "shared" ]; then
        SHARED_FLAGS="--disable-static --enable-shared"
    elif [ "$LIBRARY_TYPE" == "static" ]; then
        SHARED_FLAGS="--enable-static --disable-shared"
    fi
    # Check yasm and nasm
    if ! command -v yasm &> /dev/null && ! command -v nasm &> /dev/null; then
        NASM_YASM_FLAGS="--disable-x86asm"
    else
        NASM_YASM_FLAGS=""
    fi
    # Check libmp3lame
    check_libmp3lame=$(find /usr/ -name libmp3lame*)
    if [ -z "$check_libmp3lame" ]; then
        LIBMP3LAME_FLAGS=""
    else
        LIBMP3LAME_FLAGS="--enable-libmp3lame"
    fi
    # Check release mode
    if [ "$COMPILE_MODE"  == "debug" ]; then
        COMPILE_MODE_FLAGS="--enable-debug --disable-optimizations --disable-stripping"
    else
        COMPILE_MODE_FLAGS="--enable-optimizations --enable-stripping"
    fi

    patch -p1 -i ../../ffmpeg4.4_mlu300.patch
    rm -rf build && mkdir -pv build
    pushd build
        ../configure \
            --prefix=$INSTALL_PATH \
            --arch=$ARCH           \
            --extra-cflags="-I${NEUWARE_HOME}/include"         \
            --extra-ldflags="-L${NEUWARE_HOME}/lib64"          \
            --extra-libs="-lcnrt -lcncodec_v3 -lcndrv -lcndev" \
            $MLU_FLAGS             \
            --enable-gpl           \
            --enable-version3      \
            $SHARED_FLAGS          \
            $NASM_YASM_FLAGS $COMPILE_MODE_FLAGS \
            $LIBMP3LAME_FLAGS

        make -j$(nproc)

        SUDO=sudo
        if [ "$IN_DOCKER" = true ]; then
            if [ "$(id -u)" != "0" ]; then
                echo "Running inside Docker as non-root user. Please ensure the necessary dependencies are installed manually."
            else
                SUDO=""
            fi
        fi
        # Check permission of install directory
        if [ -w "$INSTALL_PATH" ]; then
            make install
        else
            $SUDO make install
        fi
    popd
}

clean_ffmpeg() {
    pushd $WORK_DIR/ffmpeg
        rm -rf ffmpeg-${FFMPEG_VERSION}
    popd
}

# Main
pushd ffmpeg
    extract_ffmpeg
    pushd ffmpeg-${FFMPEG_VERSION}
        compile_ffmpeg
    popd
popd
clean_ffmpeg

echo ""
echo "FFmpeg ${FFMPEG_VERSION} has been successfully installed to ${INSTALL_PATH}"
