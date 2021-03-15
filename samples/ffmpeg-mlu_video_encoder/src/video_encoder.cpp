#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <iomanip>
#include "video_encoder.h"
#include "time.h"
#include <vector>


#define CHECKFFRET(ret) \
if (ret < 0)\
{\
    av_log(nullptr, ret != AVERROR(EAGAIN) ? (ret != AVERROR_EOF ? AV_LOG_ERROR : AV_LOG_INFO) : AV_LOG_DEBUG, "%s %d : %d\n", __FILE__, __LINE__, ret);\
    return ret;\
}

using namespace std;

int VideoEncoder::initEncoder(std::string source, int width, int height, int frame_num, int device_id, int threadid, int picture_write_flag) {
    avformat_network_init();
    input_file = fopen(source.c_str(), "rb");
    if (input_file == NULL)
    {
        fprintf(stderr, "open failed\n");
        return -1;
    }
    threadId = threadid;
    encodeFrameNum = 0;
    dAVdict = NULL;
    devId = device_id;
    encodeCount = frame_num;
    durationTime = 2000.0;
    write_flag = picture_write_flag;
    //std::cout << "initEncode start" <<std::endl;

    //av_dict_set_int(&eAVdict, "trace", 1, 0);
    av_dict_set_int(&eAVdict, "device_id", devId, 0);

    encodeContext.codec = avcodec_find_encoder_by_name("h264_mluenc");
    if( !encodeContext.codec ) { 
        fprintf(stderr,"encoder find failed."); 
        return -1;
    }

    encodeContext.codecContext = avcodec_alloc_context3(encodeContext.codec);
    if (!encodeContext.codecContext) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    encodeContext.codecContext->width = width;
    encodeContext.codecContext->height = height;
    encodeContext.codecContext->time_base = (AVRational) {1, 25};
    encodeContext.codecContext->framerate = (AVRational) {25, 1};
    // encodeContext.codecContext->gop_size = 10;
    // encodeContext.codecContext->max_b_frames = 1;
    encodeContext.codecContext->frame_number = frame_num;
    encodeContext.codecContext->pix_fmt = AV_PIX_FMT_NV12;

    if( !encodeContext.codecContext ) { 
        fprintf(stderr,"avcodec alloc failed.");
        return -1;
    }

    int statCode = avcodec_open2(encodeContext.codecContext, encodeContext.codec, &eAVdict);
    if( statCode < 0 ) { 
        fprintf(stderr,"avcodec open failed.");
        return -1;
    }

    ePacket = av_packet_alloc();
    av_init_packet(ePacket);

    pFrame = av_frame_alloc();
    pFrame->format = AV_PIX_FMT_NV12;
    pFrame->width = encodeContext.codecContext->width;
    pFrame->height= encodeContext.codecContext->height;
    av_frame_get_buffer(pFrame, 32);
    //std::cout << "init encoder finished" <<std::endl;
    if (write_flag) {
        std::string stream_name = getOutputName();
        output_file = fopen(stream_name.c_str(), "wb");
    }
    return 0;
}

int VideoEncoder::encode() {
    int encret = 0;
    uint8_t* picture_buf = (uint8_t*)av_malloc(pFrame->width * pFrame->height * 3 / 2);
    stepStartTime = stepEndTime = std::chrono::steady_clock::now();
    while (encodeCount) {
        encodeStartTime = encodeEndTime = std::chrono::steady_clock::now();
        int load_len = fread(picture_buf, 1, pFrame->width * pFrame->height * 3 / 2, input_file);
        if (!load_len) {
          fseek(input_file, 0L, SEEK_SET);
          continue;
        } else if (load_len != pFrame->width * pFrame->height * 3 / 2) {
          fprintf(stderr, "invalid input size");
          break;
        }
        pFrame->data[0] = picture_buf;
        pFrame->data[1] = picture_buf + pFrame->width * pFrame->height;
        pFrame->pts = encodeFrameNum;
        encret = avcodec_send_frame(encodeContext.codecContext, pFrame);
        if (encret < 0) {
            fprintf(stderr,"Error sending a frame for encoding");
            return -1;
        }
        encodeFrameNum++;
        encodeCount--;
        while(encret >= 0) {
            encret = avcodec_receive_packet(encodeContext.codecContext, ePacket);
            if (encret == AVERROR(EAGAIN) || encret == AVERROR_EOF) {
              break;
            } else if (encret < 0) {
                fprintf(stderr, "Error during encoding\n");
                return -1;
            } 
            stepEndTime = std::chrono::steady_clock::now();
            stepEncodeFrameNum++;
            step_diff = stepEndTime - stepStartTime;
            if (step_diff.count() > durationTime) {
                printPerfStep();
                stepEncodeFrameNum = 0;
                stepStartTime = std::chrono::steady_clock::now();
            }
            encodeEndTime = std::chrono::steady_clock::now();
            std::chrono::duration<double, std::milli> tmp_time = encodeEndTime - encodeStartTime;
            encodeTime.emplace_back(tmp_time);
            if (write_flag) {
                fwrite(ePacket->data, 1, ePacket->size, output_file);
            }
            av_packet_unref(ePacket);
        }
    }
    encret = avcodec_send_frame(encodeContext.codecContext, NULL);
    if (encret < 0) {
        fprintf(stderr, "Error sending a frame for encoding");
        return -1;
    }
    while(encret >= 0) {
        encret = avcodec_receive_packet(encodeContext.codecContext, ePacket);
        if (encret == AVERROR(EAGAIN) || encret == AVERROR_EOF) {
            break;
        } else if (encret < 0) {
            fprintf(stderr, "Error during encoding\n");
            return -1;
        } 
        if (write_flag) {
            fwrite(ePacket->data, 1, ePacket->size, output_file);
        }
    }
    return 0;
}

std::string VideoEncoder::getOutputName() {
    string threadstr = "_thread_";
    string suffix = ".h264";
    return threadstr + std::to_string(threadId) + suffix;
}

void VideoEncoder::printPerf() 
{
    if (encodeTime.size() <= 0)
        return;
    std::chrono::duration<double, std::milli> encodesum = encodeTime[0];
    for(uint32_t i = 1; i < encodeTime.size(); i++) {
      encodesum += encodeTime[i];
    }
    if (encodesum.count()) {
        encodeFps =  (encodeFrameNum * 1000 * 1.f / encodesum.count());
        std::cout << "[FPS-stats] thread id " << threadId << " info :" <<std::endl;
        std::cout << "*  EncodeFps is: " << std::fixed <<setprecision(2) << encodeFps << std::endl;
        std::cout << "*  Number of images processed:" << encodeFrameNum << std::endl;
    }
}

void VideoEncoder::closeEncoder() {
    fclose(input_file);
    av_frame_free(&pFrame);

    avformat_free_context(encodeContext.formatContext);
    avcodec_close(encodeContext.codecContext);
    avcodec_free_context(&encodeContext.codecContext);

    if(eAVdict) av_dict_free(&eAVdict);
    
    // if(dPacket) av_packet_free(&dPacket);
    
    //decodeContext = {};
    //encodeContext = {};
    printPerf();
    //std::cout << "close transer." << std::endl;
}

void VideoEncoder::printPerfStep() {
    double stepFps = 0.0f;
    if (step_diff.count()) {
        stepFps = (stepEncodeFrameNum * 1000 * 1.f / step_diff.count());
    }
    cout << "[stepFpsStats]: -->thread id: " << threadId <<" -->fps: "
         << std::fixed <<setprecision(2)<< stepFps << " -->step time: " << step_diff.count();
         cout.unsetf( ios::fixed);
    cout << " -->step frame count: " << stepEncodeFrameNum;
    cout << " -->total frame count: " << encodeFrameNum;
    cout << std::endl;
}

double VideoEncoder::getActualFps() {
    if (encodeFps < 0) {
        encodeFps = 0.0f;
    }
    return encodeFps;
}
