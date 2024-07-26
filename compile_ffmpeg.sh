#!/bin/bash

print_usage() {
    echo ""
    echo "****** FFMpeg-MLU compile instructions ******"
    echo " export BANGWARE_HOME=/x/xxx"
    echo " ./compile_ffmpeg.sh <mlu_platform> <base_version>"
    echo "    <mlu_platform>: Required, set MLU300、MLU370 or MLU500. default MLU300."
    echo "    <base_version>: Optional, set 4.4 or 4.2. default 4.4."
    echo ""
    echo "    e.g. usage:"
    echo "        ./compile_ffmpeg.sh MLU300"
    echo "     or ./compile_ffmpeg.sh MLU370 4.2"
    echo "     or ./compile_ffmpeg.sh MLU500 4.4"
    echo "*****************************************************************************"
    echo ""
}

if [ $# -ne 1 ] && [ $# -ne 2 ]; then
    print_usage
    exit -1
fi

BANGWARE_HOME=""
if [ -z ${BANGWARE_HOME} ]; then
    BANGWARE_HOME="/usr/local/neuware"
else
    BANGWARE_HOME=${BANGWARE_HOME}
fi

MLU_PLATFORM=""
if [ -z $1 ]; then
    MLU_PLATFORM="MLU370"
else
    MLU_PLATFORM=$1
fi
if [[ "${MLU_PLATFORM}" != "MLU370" && "${MLU_PLATFORM}" != "MLU300" &&
      "${MLU_PLATFORM}" != "MLU500" ]]; then
    echo ""
    echo "!!! ERROR: The input parameter is not supported: ${MLU_PLATFORM} !!!"
    print_usage
    exit -1
fi

BASE_VERSION=""
if [ -z $2 ]; then
    BASE_VERSION="4.4"
else
    BASE_VERSION=$2
fi

DST_BRANCH=""
if [[ "${BASE_VERSION}" == "4.4" ]]; then
    DST_BRANCH="n4.4.4"
else
    DST_BRANCH="release/4.2"
fi

# ------------------------------------------------------------
# FFMpeg base branch
if [ -d "./ffmpeg-mlu" ]; then
   echo "ffmpeg-mlu directory exit ..."
else
   echo "ffmpeg-mlu directory not exit, now download ..."
   git clone https://gitee.com/mirrors/ffmpeg.git -b ${DST_BRANCH} --depth=1
   mv ffmpeg ffmpeg-mlu
fi

# ------------------------------------------------------------
# Make FFMpeg-MLU and compile FFMpeg-MLU
cd ffmpeg-mlu && rm -rf build &&   \
git checkout -f && git clean -f && \
git apply ../ffmpeg${BASE_VERSION}_mlu300.patch

PROJ_DIR=${PWD}
FFMPEG_BUILD_DIR=""${PROJ_DIR}"/build"
mkdir -pv "${FFMPEG_BUILD_DIR}" && cd $FFMPEG_BUILD_DIR

../configure \
  --prefix="${FFMPEG_BUILD_DIR}" \
  --extra-libs="-lpthread -lm" \
  --extra-cflags="-I${BANGWARE_HOME}/include" \
  --extra-ldflags="-L${BANGWARE_HOME}/lib64" \
  --extra-libs="-lcnrt -lcncodec_v3 -lcndrv -ldl" \
  --enable-ffplay \
  --enable-ffmpeg \
  --enable-mlu \
  --enable-mlumpp \
  --enable-gpl \
  --enable-version3 \
  --enable-nonfree \
  --enable-shared \
  --enable-static \
  --enable-debug  \
  --enable-stripping \
  --disable-x86asm \
  --enable-optimizations

make -j$(nproc) && make -j$(nproc) examples && \
make -j$(nproc) install

echo ""
echo "*****************************************************************************"
echo "compile and preparations are ready"
echo "the compiled files are in the ffmpeg-mlu folder"
echo "*****************************************************************************"
echo ""
