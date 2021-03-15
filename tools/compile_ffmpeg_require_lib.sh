#!/bin/bash
#---------------------------------------------------------------------
#apt-get update && apt-get install -y aptitude && aptitude update && \
#    aptitude install -y \
#    wget build-essential automake autoconf git libtool libvorbis-dev \
#    libass-dev libfreetype6-dev libsdl2-dev libva-dev libvdpau-dev \
#    libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev \
#    mercurial libnuma-dev texinfo zlib1g-dev \
#    cmake qtbase5-dev 
#--------------------------------------------------------------------
#@ Compile and Building FFmpeg with dependencies.

DIR=${PWD}
DEB_DIR=${DIR}/3rdparty
WORKDIR=${DIR}/ffmpeg_build
mkdir -pv ${WORKDIR}
rm -rf ${WORKDIR}/

echo ""
echo "--------------install nasm----------------"
cd ${DEB_DIR}/nasm && ./autogen.sh && ./configure --prefix="${WORKDIR}" &&  make -j && make install

echo ""
echo "--------------install yasm----------------"
cd ${DEB_DIR}/yasm && ./configure --prefix="${WORKDIR}" && make -j && make install

export PATH=$PATH:${WORKDIR}/bin

echo ""
echo "--------------install x264---------------"
cd ${DEB_DIR}/x264_2016 && ./configure --prefix="${WORKDIR}" --enable-shared --enable-pic && make && make install
#cd ${DIR}/x264 && ./configure --prefix="${WORKDIR}" --enable-static --enable-pic && make && make install

echo ""
echo "---------------install aac---------------"
cd ${DEB_DIR}/fdk-aac && autoreconf -fiv && ./configure --prefix="${WORKDIR}" --disable-shared && make -j && make install

echo ""
echo "---------------install lame--------------"
cd ${DEB_DIR}/lame && ./configure --prefix="${WORKDIR}" --disable-shared --enable-nasm && make -j && make install

echo ""
echo "---------------install opus--------------"
cd ${DEB_DIR}/opus && ./autogen.sh && ./configure --prefix="${WORKDIR}" --disable-shared && make -j && make install

echo "====install deb done===="
echo ""
cd ${DIR}

