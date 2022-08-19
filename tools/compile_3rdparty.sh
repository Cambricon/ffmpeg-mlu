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

###############################
# build nasm
###############################
echo ""
echo "--------------install nasm----------------"
cd ${DEB_DIR}/nasm
./autogen.sh && ./configure --prefix="${WORKDIR}"
make -j && make install
cd -

###############################
# build yasm
###############################
echo ""
echo "--------------install yasm----------------"
cd ${DEB_DIR}/yasm
./configure --prefix="${WORKDIR}"
make -j && make install
cd -

###############################
# build aac
###############################
echo ""
echo "---------------install aac---------------"
cd ${DEB_DIR}/fdk-aac
./autogen.sh && ./configure --prefix="${WORKDIR}" --enable-shared
make -j && make install
cd -

###############################
# build lame
###############################
echo ""
echo "---------------install lame--------------"
cd ${DEB_DIR}/lame
./configure --prefix="${WORKDIR}" --enable-shared --enable-nasm
make -j && make install
cd -

###############################
# build opus
###############################
echo ""
echo "---------------install opus--------------"
cd ${DEB_DIR}/opus
./autogen.sh && ./configure --prefix="${WORKDIR}" --enable-shared
make -j && make install
cd -


export PATH=$PATH:${WORKDIR}/bin


###############################
# build x264 decoder
###############################
echo ""
echo "--------------install x264---------------"
cd ${DEB_DIR}/x264
./configure --prefix="${WORKDIR}" --enable-shared --enable-pic
make && make install
cd -

###############################
# build x265 decoder
###############################
echo ""
echo "--------------install x265---------------"
cd ${DEB_DIR}/x265/build/linux
cmake -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX=${WORKDIR} -DENABLE_SHARED=on ../../source
make -j && make install
cd -

###############################
# build xavs2 decoder
###############################
echo ""
echo "--------------install xavs2---------------"
cd ${DEB_DIR}/xavs2/build/linux
./configure --prefix=${WORKDIR} --enable-pic --enable-shared --disable-asm
make -j && make install
cd -

###############################
# build davs2 decoder
###############################
echo ""
echo "--------------install davs2---------------"
cd ${DEB_DIR}/davs2/build/linux
./configure --prefix=${WORKDIR} --enable-pic --enable-shared --disable-asm
make -j && make install
cd -

echo "====install deb done===="
echo ""
cd ${DIR}

