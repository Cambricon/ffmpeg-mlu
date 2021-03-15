#!/usr/bin/env bash

# ----------------------get FFmpeg (ver.4.2)---------------------- #
if [ -d "./ffmpeg-mlu/" ];then
   echo "ffmpeg-mlu directory exit ..."
else
   echo "ffmpeg-mlu directory not exit, now download ..."
   git clone https://gitee.com/mirrors/ffmpeg.git -b release/4.2 --depth=1
   mv ffmpeg ffmpeg-mlu
fi

PROJ_DIR=${PWD}
# ------------------------compile ffmpeg-mlu---------------------- #
# NOTICE, here you can adjust the FFmpeg building configuration parameters
# For example, if you decided not to use VAAPI (video acceleration API) use --disable-vaapi.
# Check out available flags on the official FFmpeg website.
cd ffmpeg-mlu && \
git checkout -f && git clean -f && \
git apply ../ffmpeg4.2_mlu.patch

FFMPEG_LIB_DIR=""${PWD}"/build"
mkdir -pv "${FFMPEG_LIB_DIR}"

cd $FFMPEG_LIB_DIR

../configure \
  --prefix="${FFMPEG_LIB_DIR}" \
  --extra-libs="-lpthread -lm" \
  --extra-cflags="-I/usr/local/neuware/include" \
  --extra-ldflags="-L/usr/local/neuware/lib64" \
  --extra-libs="-lcnrt -lcncodec -lcndrv -ldl" \
  --enable-ffplay \
  --enable-ffmpeg \
  --enable-mlumpp \
  --enable-gpl \
  --enable-version3 \
  --enable-nonfree \
  --enable-libfreetype \
  --enable-shared \
  --disable-static \
  --enable-stripping \
  --enable-optimizations
  # --cpu=native \
  # --enable-libx264 \
  # --enable-libx265 \
  # --enable-debug=3 \
  # --disable-debug \
  # --disable-vaapi

make -j$(nproc) && \
make -j$(nproc) install && \
make -j$(nproc) distclean
# ------------------------prepare for rel---------------------- #
rm -rf ../../build
mv -f ${FFMPEG_LIB_DIR} ../../

cd ${PROJ_DIR}
SAMPLES_DIR=""${PROJ_DIR}"/samples/"

function clean_soft_link(){
   for file in `ls $1`
   do
      echo "clean: $1${file}"
      rm -rf $1${file}/3rdparty/ffmpeg
      rm -rf $1${file}/3rdparty/ffmpeg
   done
}

function get_samples_file(){
   for file in `ls $1`
   do
      echo "create ln -s $1${file}"
      mkdir -p $1${file}/3rdparty/ffmpeg
      ln -s ${PROJ_DIR}/build/include $1${file}/3rdparty/ffmpeg/include
      ln -s ${PROJ_DIR}/build/lib $1${file}/3rdparty/ffmpeg/lib
   done
}
clean_soft_link ${SAMPLES_DIR}
get_samples_file ${SAMPLES_DIR}
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${PROJ_DIR}/build/lib
echo "*********compile and prepair ready*********"
echo
