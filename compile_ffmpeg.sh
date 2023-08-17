#!/usr/bin/env bash

PARAM_NUM=$#

function compile_script_helper() {
   echo ""
   echo "***** ffmpeg compile script instructions *****"
   echo " ./compile_ffmpeg.sh <mlu_platform> <bangware_home>"
   echo "    <mlu_platform>: Required. Choose from MLU200 or MLU370, depending on your situation"
   echo "    <bangware_home>: Optional. if you have set \$BANGWARE_HOME in your environment variable, it can be null."
   echo "                    Otherwise the default bangware home path is /usr/local/neuware"
   echo ""
   echo "    e.g. if your hardware platform is MLU200, use this:"
   echo "      ./compile_ffmpeg.sh MLU200"
   echo "    e.g. if your hardware platform is MLU370 and bangware path is /tmp/bangware, use this:"
   echo "      ./compile_ffmpeg.sh MLU370 /tmp/bangware"
   echo "**********************************************"
   echo ""
}

if [ ${PARAM_NUM} -ne 1 ] && [ ${PARAM_NUM} -ne 2 ];then
   compile_script_helper
   exit 0
fi

BANGWARE_HOME_PATH=""
if [ -z ${BANGWARE_HOME} ]; then
   BANGWARE_HOME_PATH="/usr/local/neuware"
else
   BANGWARE_HOME_PATH=${BANGWARE_HOME}
fi

MLU_PLATFORM="$1"
if [ ${PARAM_NUM} -eq 2 ]; then
   BANGWARE_HOME_PATH=$2
fi

FFMPEG_MLU_VER=""
FFMPEG_MLU_LIB_VER=""
if [[ "${MLU_PLATFORM}" == "MLU370" || "${MLU_PLATFORM}" == "MLU300" || "${MLU_PLATFORM}" == "MLU500" ]];then
   FFMPEG_MLU_VER="300"
   FFMPEG_MLU_LIB_VER="_v3"
elif [[ "${MLU_PLATFORM}" == "MLU200" || "${MLU_PLATFORM}" == "MLU220" || "${MLU_PLATFORM}" == "MLU270" ]];then
   FFMPEG_MLU_VER="200"
else
   echo "!!! ERROR: The input parameter is not supported: ${MLU_PLATFORM} !!!"
   compile_script_helper
   exit 0
fi

# ----------------------get FFmpeg (ver.4.2)---------------------- #
if [ -d "./ffmpeg-mlu/" ];then
   echo "ffmpeg-mlu directory exit ..."
else
   echo "ffmpeg-mlu directory not exit, now download ..."
   git clone https://gitee.com/mirrors/ffmpeg.git -b release/4.2 --depth=1
   mv ffmpeg ffmpeg-mlu
fi

# ------------------------compile ffmpeg-mlu---------------------- #
cd ffmpeg-mlu && rm -rf build &&\
git checkout -f && git clean -f && \
git apply ../ffmpeg4.2_mlu${FFMPEG_MLU_VER}.patch

PROJ_DIR=${PWD}
FFMPEG_BUILD_DIR=""${PROJ_DIR}"/build"
mkdir -pv "${FFMPEG_BUILD_DIR}"

cd $FFMPEG_BUILD_DIR

../configure \
  --prefix="${FFMPEG_BUILD_DIR}" \
  --extra-libs="-lpthread -lm" \
  --extra-cflags="-I${BANGWARE_HOME_PATH}/include" \
  --extra-ldflags="-L${BANGWARE_HOME_PATH}/lib64" \
  --extra-libs="-lcnrt -lcncodec${FFMPEG_MLU_LIB_VER} -lcndrv -ldl" \
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

make -j$(nproc) && \
make -j$(nproc) examples && \
make -j$(nproc) install

echo "******** compile and preparations are ready ******** "
echo "******** the compiled files are in the ffmpeg-mlu folder ******** "
