#!/bin/bash
#---------------------------------------------------------------------
#apt-get update && apt-get install -y aptitude && aptitude update && \
#    aptitude install -y \
#    wget build-essential automake autoconf git libtool libvorbis-dev \
#    libass-dev libfreetype6-dev libsdl2-dev libva-dev libvdpau-dev \
#    libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev \
#    mercurial libnuma-dev texinfo zlib1g-dev \
#    cmake qtbase5-dev yasm nasm libmp3lame-dev
#--------------------------------------------------------------------
#@ Compile and Building FFmpeg with dependencies.

set -e
CUR_DIR=$(dirname $(readlink -f "${BASH_SOURCE[0]}") )
WORKDIR=${CUR_DIR}/install
rm -rf ${WORKDIR} && mkdir -pv ${WORKDIR}

###############################
# build nasm
###############################
echo "Info: install nasm"
tar -xf nasm.tar.gz
pushd ${CUR_DIR}/nasm
    ./autogen.sh && ./configure --prefix="${WORKDIR}"
    make -j && make install
popd

###############################
# build yasm
###############################
echo "Info: install yasm"
tar -xf yasm.tar.gz
pushd ${CUR_DIR}/yasm
    ./configure --prefix="${WORKDIR}"
    make -j && make install
popd

###############################
# build aac
###############################
echo "Info: install aac"
tar -xf fdk-aac.tar.gz
pushd ${CUR_DIR}/fdk-aac
    ./autogen.sh && ./configure --prefix="${WORKDIR}" --enable-shared
    make -j && make install
popd

###############################
# build lame
###############################
echo "Info: install lame"
tar -xf lame.tar.gz
pushd ${CUR_DIR}/lame
    ./configure --prefix="${WORKDIR}" --enable-shared --enable-nasm
    make -j && make install
popd

###############################
# build opus
###############################
echo "Info: install opus"
tar -xf opus.tar.gz
pushd ${CUR_DIR}/opus
    ./autogen.sh && ./configure --prefix="${WORKDIR}" --enable-shared
    make -j && make install
popd

export PATH=$PATH:${WORKDIR}/bin

###############################
# build x264 decoder
###############################
echo "Info: install x264"
tar -xf x264.tar.gz
pushd ${CUR_DIR}/x264
    ./configure --prefix="${WORKDIR}" --enable-shared --enable-pic -enable-strip
    make && make install
popd

###############################
# build x265 decoder
###############################
echo "Info: install x265"
tar -xf x265.tar.gz
pushd ${CUR_DIR}/x265/build/linux
    cmake -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX=${WORKDIR} -DENABLE_SHARED=on ../../source
    make -j && make install
popd

###############################
# build xavs2 decoder
###############################
echo "Info: install xavs2"
tar -xf xavs2.tar.gz
pushd ${CUR_DIR}/xavs2/build/linux
    ./configure --prefix=${WORKDIR} --enable-pic --enable-shared --disable-asm
    make -j && make install
popd

###############################
# build davs2 decoder
###############################
echo "Info: install davs2"
tar -xf davs2.tar.gz
pushd ${CUR_DIR}/davs2/build/linux
    ./configure --prefix=${WORKDIR} --enable-pic --enable-shared --disable-asm
    make -j && make install
popd

echo "Install ffmpeg 3rdparty library done"
