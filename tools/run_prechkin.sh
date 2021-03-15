#!/usr/bin/env bash

cd ../build/bin
function show_frame_info(){
    BANNER_MESSAGE_TOP="=============show frame message==============="
    BANNER_MESSAGE_BOTTOM="=============================================="

    echo ${BANNER_MESSAGE_TOP}
    ./ffprobe -show_streams $1
    echo ${BANNER_MESSAGE_BOTTOM}
}

function is_md5_equal(){
    md5_target=`md5sum $2 | cut -d " " -f1`
    md5_refer=`md5sum $3 | cut -d " " -f1`
    right_message="[PASS]:${1}"
    wrong_message="[FAILED]:${1}"
    if [[ $md5_target == $md5_refer ]];then
        echo -e "\033[40m\033[42m${right_message}\033[0m"
    else
        echo -e "\033[41m\033[37m${wrong_message}\033[0m"
    fi
}

function build_clean(){
    if [ -d "./build/" ];then
        rm -r ./build/*
    else
        mkdir build
    fi
}

# ----------------------H264 decoder---------------------- #
./ffmpeg -y -vsync 0 -loglevel quiet -c:v h264_mludec  -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -vframes 10 h264_dec_out_target.yuv
./ffmpeg -y -vsync 0 -loglevel quiet -c:v h264 -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -pix_fmt nv12 -vframes 10 h264_dec_out_refer.yuv
is_md5_equal H264_decoder h264_dec_out_target.yuv h264_dec_out_refer.yuv

# ----------------------HEVC decoder---------------------- #
./ffmpeg -y -vsync 0 -loglevel quiet -c:v hevc_mludec -i ../../mlu_op/data/images/jellyfish-5-mbps-hd-hevc.mkv -vframes 10 hevc_dec_out_target.yuv
./ffmpeg -y -vsync 0 -loglevel quiet -c:v hevc -i ../../mlu_op/data/images/jellyfish-5-mbps-hd-hevc.mkv -pix_fmt nv12 -vframes 10 hevc_dec_out_refer.yuv
is_md5_equal HEVC_decoder hevc_dec_out_target.yuv hevc_dec_out_refer.yuv

# ----------------------JPEG decoder---------------------- #
./ffmpeg -y -vsync 0 -loglevel quiet -c:v mjpeg_mludec -i ../../mlu_op/data/images/1920_1080_1.jpg -pix_fmt yuv420p jpeg_dec_out_target.yuv
./ffmpeg -y -vsync 0 -loglevel quiet -c:v mjpeg -i ../../mlu_op/data/images/1920_1080_1.jpg -pix_fmt yuv420p jpeg_dec_out_refer.yuv
is_md5_equal JPEG_decoder jpeg_dec_out_target.yuv jpeg_dec_out_refer.yuv

# ----------------------H264 encoder---------------------- #
./ffmpeg -y -benchmark -loglevel quiet -re -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -c:v h264_mluenc -vframes 10 h264_enc_out_target.h264
./ffmpeg -y -benchmark -loglevel quiet -re -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -c:v h264 -vframes 10 h264_enc_out_refer.h264

./ffmpeg -y -loglevel quiet -f rawvideo -s 1920x1080 -pix_fmt nv12 -i ../../mlu_op/data/images/1920_1080_nv12_1.yuv -c:v h264_mluenc h264_enc_raw_out_target.h264
./ffmpeg -y -loglevel quiet -f rawvideo -s 1920x1080 -pix_fmt nv12 -i ../../mlu_op/data/images/1920_1080_nv12_1.yuv -c:v h264 h264_enc_raw_out_refer.h264

is_md5_equal H264_encoder h264_enc_out_target.h264 h264_enc_out_refer.h264
is_md5_equal H264_encoder_rawdata h264_enc_raw_out_target.h264 h264_enc_raw_out_refer.h264

# ----------------------JPEG encoder---------------------- #
./ffmpeg -y -loglevel quiet -f rawvideo -s 1920x1080 -i ../../mlu_op/data/images/1920_1080_yuv420p_1.yuv -c:v mjpeg_mluenc -pix_fmt nv12 jpeg_enc_raw_out_target.jpeg
./ffmpeg -y -loglevel quiet -f rawvideo -s 1920x1080 -i ../../mlu_op/data/images/1920_1080_yuv420p_1.yuv -c:v mjpeg jpeg_enc_raw_out_refer.jpeg
is_md5_equal JPEG_encoder_rawdata jpeg_enc_raw_out_target.jpeg jpeg_enc_raw_out_refer.jpeg

./ffmpeg -y -loglevel quiet -c:v mjpeg_mludec -i ../../mlu_op/data/images/1920_1080_1.jpg -c:v mjpeg_mluenc -pix_fmt nv12 jpeg_dec_enc_raw_out_target.jpeg
./ffmpeg -y -loglevel quiet -c:v mjpeg -i ../../mlu_op/data/images/1920_1080_1.jpg -c:v mjpeg -pix_fmt nv12 jpeg_dec_enc_raw_out_refer.jpeg
is_md5_equal JPEG_decoder_encoder jpeg_dec_enc_raw_out_target.jpeg jpeg_dec_enc_raw_out_refer.jpeg

./ffmpeg -y -loglevel quiet -benchmark -re -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -c:v mjpeg_mluenc -vframes 1 jpeg_video_enc_target.jpeg
./ffmpeg -y -loglevel quiet -benchmark -re -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -c:v mjpeg -vframes 1 jpeg_video_enc_refer.jpeg
is_md5_equal JPEG_video_encoder jpeg_video_enc_target.jpeg jpeg_video_enc_refer.jpeg

./ffmpeg -y -loglevel quiet -c:v h264_mludec -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -pix_fmt nv12 -c:v mjpeg_mluenc -vframes 1 jpeg_video_h264dec_enc_target.jpeg
./ffmpeg -y -loglevel quiet -c:v h264 -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -pix_fmt nv12 -c:v mjpeg -vframes 1 jpeg_video_h264dec_enc_refer.jpeg
is_md5_equal JPEG_video_h264dec_encoder jpeg_video_h264dec_enc_target.jpeg jpeg_video_h264dec_enc_refer.jpeg

# ----------------------HW Transcode---------------------- #
./ffmpeg -y -loglevel quiet -vsync 0 -c:v h264_mludec -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -c:a copy -c:v h264_mluenc -b:v 5M transcode_target.h264
./ffmpeg -y -loglevel quiet -vsync 0 -c:v h264_mlu -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -c:a copy -c:v h264_mlu -b:v 5M transcode_refer.h264
is_md5_equal H624_transcode transcode_target.h264 transcode_refer.h264

./ffmpeg -y -loglevel quiet -vsync 0 -c:v h264_mludec -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -c:a copy -c:v h264_mluenc -b:v 1M -s 1280x720 transcode_resize_target.h264
./ffmpeg -y -loglevel quiet -vsync 0 -c:v h264 -i ../../mlu_op/data/images/jellyfish-3-mbps-hd-h264.mkv -c:a copy -c:v h264 -b:v 1M -s 1280x720 -pix_fmt nv12 transcode_resize_refer.h264
is_md5_equal H624_resize_transcode transcode_resize_target.h264 transcode_resize_refer.h264


cd ../../samples/
echo -e "\n"
echo -e "\033[5;4;37m\033[46m ==============SAMPLES_TEST=============== \033[0m" 

# ----------------------samples-JPEG_encoder---------------------- #
echo "----------------------samples-JPEG_encoder----------------------"
cd ffmpeg-mlu_jpeg_encoder
build_clean
cd build
cmake ..
make -j
./jpeg_encoder ../../../mlu_op/data/images/1920_1080_nv12_1.yuv 1920 1080 1 0 1 0
cd ../../

# ----------------------samples-vid2jpeg_transcode---------------------- #
echo "----------------------samples-vid2jpeg_transcode----------------------"
cd ffmpeg-mlu_vid2jpeg_transcode
build_clean
cd build
cmake ..
make -j
mkdir output
./jpeg_trans ../../../mlu_op/data/images/jellyfish_5mbpd_cif_352_288.mp4 0 1 0
cd ../../

# ----------------------samples-vid2vid_transcode2---------------------- #
echo "----------------------samples-vid2vid_transcode2----------------------"
cd ffmpeg-mlu_vid2vid_transcode2
build_clean
cd build
cmake ..
make -j
./transcode ../input_rtsp.txt
cd ../../

# ----------------------samples-video_decoder---------------------- #
echo "----------------------samples-video_decoder----------------------"
cd ffmpeg-mlu_video_decoder
build_clean
cd build
cmake ..
make -j
./decoder ../../../mlu_op/data/images/jellyfish_5mbpd_cif_352_288.mp4 0 0 1
cd ../../

# ----------------------samples-video_encoder---------------------- #
echo "----------------------samples-video_encoder----------------------"
cd ffmpeg-mlu_video_encoder
build_clean
cd build
cmake ..
make -j
./video_encoder ../../../mlu_op/data/images/video.yuv 1920 1080 60 0 1 1
cd ../../

# ----------------------samples-video_encoder_open_close---------------------- #
echo "----------------------samples-video_encoder_open_close----------------------"
cd ffmpeg-mlu_video_encoder_open_close
build_clean
cd build
cmake ..
make -j
./encoder_open_close ../../../mlu_op/data/images/720p.yuv 1280 720 1 0 1 1
cd ../../

echo "======done======"
echo ""
