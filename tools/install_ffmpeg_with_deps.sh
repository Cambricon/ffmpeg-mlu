#!/usr/bin/env bash
# version 1.0.1
# Installation commands were obtained from the official
# FFmpeg compilation guides (see https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu).

# Building FFmpeg with dependencies (e.g. hardware encoding (vaapi),
# encoding (libx264, libx265), Video4Linux API backend, etc.

sudo apt-get update -qq && sudo apt-get -y install \
autoconf \
automake \
build-essential \
libtool \
pkg-config \
texinfo \
zlib1g-dev \
mercurial \
libdrm-dev \
libogg-dev \
libx11-dev \
libperl-dev \
libpciaccess-dev \
libpciaccess0 \
xorg-dev \
libfreetype6-dev \
libsdl2-dev \
libtheora-dev \
libvdpau-dev \
libxcb1-dev \
libxcb-shm0-dev \
libxcb-xfixes0-dev \
libv4l-dev \
v4l-utils
# libva-dev

# Installation variables
FFMPEG_DIR=""${PWD}"/../libs"

FFMPEG_SRC_DIR="${FFMPEG_DIR}"/ffmpeg_sources
FFMPEG_BIN_DIR="${FFMPEG_DIR}"/ffmpeg

# creating the directories
mkdir -pv "${FFMPEG_SRC_DIR}"
mkdir -pv "${FFMPEG_BIN_DIR}"

# NASM (v2.14)
cd "${FFMPEG_SRC_DIR}"

if [ ! -d ${FFMPEG_SRC_DIR}/nasm-2.14 ]; then
    wget http://www.nasm.us/pub/nasm/releasebuilds/2.14/nasm-2.14.tar.gz && \
    tar xzvf nasm-2.14.tar.gz
fi

cd nasm-2.14 && \
./autogen.sh && \
./configure --prefix="${FFMPEG_BIN_DIR}" --bindir="${FFMPEG_BIN_DIR}" && \
make -j$(nproc) VERBOSE=1 && \
make -j$(nproc) install && \
make -j$(nproc) distclean

# H.264 (libx264) - video codec (encoding and decoding)
cd "${FFMPEG_SRC_DIR}"

if [ ! -d ${FFMPEG_SRC_DIR}/x264 ]; then
    git clone http://git.videolan.org/git/x264.git -b stable
fi

cd x264 && \
PATH="${FFMPEG_BIN_DIR}:$PATH" ./configure --prefix="${FFMPEG_BIN_DIR}" --bindir="${FFMPEG_BIN_DIR}" --enable-static --disable-opencl --enable-pic && \
PATH="${FFMPEG_BIN_DIR}:$PATH" make -j$(nproc) VERBOSE=1 && \
make -j$(nproc) install VERBOSE=1 && \
make -j$(nproc) distclean

# HEVC (libx265) - video codec (encoding and decoding)
cd "${FFMPEG_SRC_DIR}"

if [ ! -d ${FFMPEG_SRC_DIR}/x265-2.8 ]; then
    wget -O x265-2.8.tar.gz https://github.com/videolan/x265/archive/2.8.tar.gz && \
    tar xzvf x265-2.8.tar.gz
fi

cd x265-2.8/build/linux && \
PATH="${FFMPEG_BIN_DIR}:$PATH" cmake -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX="${FFMPEG_BIN_DIR}" -DENABLE_SHARED=off ../../source && \
PATH="${FFMPEG_BIN_DIR}:$PATH" make -j$(nproc) VERBOSE=1 && \
make -j$(nproc) install VERBOSE=1 && \
make -j$(nproc) clean VERBOSE=1

# The following section is written to enable 
# hardware encoding capabilities in FFmpeg (Intel QuickSync)

# sudo add-apt-repository ppa:oibaf/graphics-drivers
# sudo apt-get update && sudo apt-get -y upgrade && sudo apt-get -y dist-upgrade

# # libva
# git clone https://github.com/01org/libva
# cd libva
# ./autogen.sh
# ./configure --enable-nonfree --enable-libx264 --enable-libx265 --enable-vaapi
# make -j$(nproc) VERBOSE=1
# sudo make -j$(nproc) install
# cd ..

# # cmrt
# git clone https://github.com/01org/cmrt
# cd cmrt
# ./autogen.sh
# ./configure
# make -j$(nproc) VERBOSE=1
# sudo make -j$(nproc) install
# cd ..

# # intel-hybrid-driver
# git clone https://github.com/01org/intel-hybrid-driver
# cd intel-hybrid-driver
# ./autogen.sh
# ./configure
# make -j$(nproc) VERBOSE=1
# sudo make -j$(nproc) install
# cd ..

# # intel-vaapi-driver
# git clone https://github.com/01org/intel-vaapi-driver
# cd intel-vaapi-driver
# ./autogen.sh
# ./configure --enable-hybrid-codec
# make -j$(nproc) VERBOSE=1
# sudo make -j$(nproc) install
# cd ..

# # libva-utils
# git clone https://github.com/intel/libva-utils
# cd libva-utils
# ./autogen.sh
# ./configure
# make -j$(nproc) VERBOSE=1
# sudo make -j$(nproc) install
# cd ..

# sudo ldconfig

# echo "LIBVA_DRIVER_NAME=i965" >> ~/.bashrc
# echo "LIBVA_DRIVERS_PATH=/usr/local/lib/dri" >> ~/.bashrc

# FFmpeg (ver.4.2)
# cd "${FFMPEG_SRC_DIR}"

# if [ ! -d ${FFMPEG_SRC_DIR}/ffmpeg-mlu ]; then
#     git clone https://gitee.com/mirrors/ffmpeg.git -b release/4.2 --depth=1
#     mv ffmpeg ffmpeg-mlu
# fi

# # NOTICE, here you can adjust the FFmpeg building configuration parameters
# # For example, if you decided not to use VAAPI (video acceleration API) use --disable-vaapi.
# # Check out available flags on the official FFmpeg website.
# cd ffmpeg-mlu && \
# git checkout -f && git clean -f && \
# git apply ../../../../../ffmpeg4.2_mlu.patch && \
# PATH="${FFMPEG_BIN_DIR}:$PATH" PKG_CONFIG_PATH="${FFMPEG_BIN_DIR}/lib/pkgconfig:/usr/local/lib/pkgconfig" ./configure \
#   --pkg-config-flags="--static" \
#   --prefix="${FFMPEG_BIN_DIR}" \
#   --extra-cflags="-I${FFMPEG_BIN_DIR}/include" \
#   --extra-ldflags="-L${FFMPEG_BIN_DIR}/lib" \
#   --extra-cflags="-I/usr/local/include" \
#   --extra-ldflags="-L/usr/local/lib" \
#   --extra-libs="-lpthread -lm" \
#   --extra-cflags="-I/usr/local/neuware/include" \
#   --extra-ldflags="-L/usr/local/neuware/lib64" \
#   --extra-libs="-lcnrt -lcncodec -lcndrv -ldl" \
#   --bindir="${FFMPEG_BIN_DIR}" \
#   --enable-debug=3 \
#   --disable-vaapi \
#   --disable-debug \
#   --enable-ffplay \
#   --enable-ffmpeg \
#   --enable-mlumpp \
#   --enable-gpl \
#   --enable-version3 \
#   --cpu=native \
#   --enable-libx264 \
#   --enable-libx265 \
#   --enable-nonfree \
#   --enable-libfreetype

# PATH="${FFMPEG_BIN_DIR}:$PATH" make -j$(nproc) && \
# make -j$(nproc) install && \
# make -j$(nproc) distclean
