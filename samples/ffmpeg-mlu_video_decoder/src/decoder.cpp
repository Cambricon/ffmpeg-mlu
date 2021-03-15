#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <iomanip>
#include "decoder.h"
#include "time.h"

#define PRINT_TIME 0
#define PRINT_PERF 1

#define CHECKFFRET(ret) \
if (ret < 0)\
{\
    av_log(nullptr, ret != AVERROR(EAGAIN) ? (ret != AVERROR_EOF ? AV_LOG_ERROR : AV_LOG_INFO) : AV_LOG_DEBUG, "%s %d : %d\n", __FILE__, __LINE__, ret);\
    return ret;\
}

using namespace std;
void Decoder::decode(std::string source, int devId_, int thread_id, bool isDump) {

    decodeId++;
    if (devId >= 0) {
        devId = devId_;
    }

    threadId = thread_id;
    durationTime = 5000.0;
    isDumpFile = isDump;
    int ret;
    if ((ret = initDecode(source)) < 0) {
        cout << "init decode fail :" << ret << endl;
        return;
    }
    startTime = endTime = stepStartTime = stepEndTime = std::chrono::steady_clock::now();
    if ((ret = decoding()) < 0) {
        cout << "decoding fail :" << ret << endl;
        return;
    }
    endTime = std::chrono::steady_clock::now();
    closeDecode();
}

int Decoder::initDecode(std::string source) {

    AVDictionary *decoder_opts = nullptr;
    AVStream *video = nullptr;
    AVDictionary *options = nullptr;
    int ret;
    if ((ret = avformat_network_init()) != 0) {
        cout << "avformat_network_init failed, ret: " << ret << endl;
        return ret;
    }

    pFormatCtx = avformat_alloc_context();
    if (!strncmp(source.c_str(), "rtsp", 4) || !strncmp(source.c_str(), "rtmp", 4)) {
        std::cout << "decode rtsp/rtmp stream " << std::endl;
        av_dict_set(&options, "buffer_size", "1024000", 0);
        av_dict_set(&options, "max_delay", "500000", 0);
        av_dict_set(&options, "stimeout", "20000000", 0);
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
    } else {
        std::cout << "decode local file stream " << std::endl;
        av_dict_set(&options, "stimeout", "20000000", 0);
    }
    // ret = avformat_open_input(&pFormatCtx, source.c_str(), nullptr, &pAvDict);
    ret = avformat_open_input(&pFormatCtx, source.c_str(), nullptr, &options);
    if (ret != 0) {
        av_dict_free(&options);
        cout << "avformat_open_input failed, ret: " << ret << endl;
        return ret;
    }
    av_dict_free(&options);
    ret = avformat_find_stream_info(pFormatCtx, nullptr);
    if (ret < 0) {
        cout << "avformat_find_stream_info failed, ret: " << ret << endl;
        return ret;
    }
    if ((ret = findVideoStreamIndex()) < 0) {
        cout << "findVideoStreamIndex failed, ret: " << ret << endl;
        return ret;
    }

    // Get a pointer to the codec context for the video stream
    video = pFormatCtx->streams[videoStream];

    // Find the decoder for the video stream
    AVCodec *pCodec = nullptr;
    switch(video->codecpar->codec_id) {
        case AV_CODEC_ID_H264:
            pCodec = avcodec_find_decoder_by_name("h264_mludec");
            break;
        case AV_CODEC_ID_HEVC:
            pCodec = avcodec_find_decoder_by_name("hevc_mludec");
            break;
        case AV_CODEC_ID_VP8:
            pCodec = avcodec_find_decoder_by_name("vp8_mludec");
            break;
        case AV_CODEC_ID_VP9:
            pCodec = avcodec_find_decoder_by_name("vp9_mludec");
            break;
        case AV_CODEC_ID_MJPEG:
            pCodec = avcodec_find_decoder_by_name("mjpeg_mludec");
            break;
        default:
            pCodec = avcodec_find_decoder(video->codecpar->codec_id);
            break;
    }
    if(pCodec == nullptr) {
        cout << "Unsupported codec!" << endl;
        return Constat::system_error; // Codec not found
    }

    // Copy context
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if(avcodec_parameters_to_context(pCodecCtx, video->codecpar) != 0) {
        cout << "Couldn't copy codec context!" << ret << endl;
        return Constat::system_error;
    }

    // Open codec
    av_dict_set_int(&decoder_opts, "device_id", devId, 0);
    // av_dict_set_int(&decoder_opts, "trace", 1, 0);
    
    // pCodecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    // Notice: This 'flags' is for set low delay decode. When decoding get blocked, try to uncomment the 'flags'.

    if(avcodec_open2(pCodecCtx, pCodec, &decoder_opts) < 0) {
        cout << "Could not open codec!" << ret << endl;
        return Constat::system_error;
    }

    if(pCodecCtx->framerate.den > 0) {
        int fps = (int)(pCodecCtx->framerate.num / pCodecCtx->framerate.den);
        if(decodeFps == 0) {
            decodeFps = fps;
        }
        skip = fps / decodeFps;
    }

    // Allocate video frame
    pFrame=av_frame_alloc();

    // Allocate an AVFrame structure
    pFrameRGB=av_frame_alloc();

    // Determine required buffer size and allocate buffer
    imgWidth = pCodecCtx->width;
    imgHeight = pCodecCtx->height;
    imgSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
    buffer = (uint8_t *)av_malloc(imgSize * sizeof(uint8_t));

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset
    // of AVPicture
    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, AV_PIX_FMT_RGB24, imgWidth, imgHeight, 1);

    // initialize SWS context for software scaling
    sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                             pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                             AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    cout << "[Source]: " << source << ", find video stream idx: " << videoStream
    << ", width: " << imgWidth << ", height: " << imgHeight << endl;
    return Constat::ok;
}

int Decoder::findVideoStreamIndex() {
    for (size_t i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            return 0;
        }
    }
    return -1;
}

int Decoder::decoding() {
    AVPacket packet;
    av_init_packet(&packet);
    #if PRINT_TIME
    std::chrono::time_point<std::chrono::steady_clock> begin_time;
    std::chrono::time_point<std::chrono::steady_clock> end_time;
    #endif
    while(1) {
        int ret = av_read_frame(pFormatCtx, &packet);
        if (ret < 0) {
            if (ret != AVERROR_EOF) {
                cout << "av_read_frame failed, ret: " << ret << endl;
            } else {
                cout << "[thread: " << threadId << "]" << " -- av_read_frame ret eof!" << endl;
            }
        }
        if (packet.stream_index != videoStream) {
            av_packet_unref(&packet);
            continue;
        }
        #if PRINT_TIME
        begin_time = std::chrono::steady_clock::now();
        #endif
        ret = avcodec_send_packet(pCodecCtx, &packet);
        #if PRINT_TIME
        end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> diff = end_time - begin_time;
        std::cout << "[send   packet:] " << diff.count() << " ms" << std::endl;
        #endif
        CHECKFFRET(ret);
        while (ret >= 0) {
            #if PRINT_TIME
            begin_time = std::chrono::steady_clock::now();
            #endif
            ret = avcodec_receive_frame(pCodecCtx, pFrame);
            #if PRINT_TIME
            end_time = std::chrono::steady_clock::now();
            diff = end_time - begin_time;
            std::cout << "[receive frame:] " << diff.count() << " ms" << std::endl;
            #endif
            if (ret < 0 && ret == AVERROR_EOF) {
                return Constat::ok;
            } else if (ret == 0) {
                if(isDumpFile) {
                    sws_scale(sws_ctx, (const uint8_t *const *)pFrame->data,
                        pFrame->linesize, 0, imgHeight, pFrameRGB->data, pFrameRGB->linesize);
                    saveImage(getImageName());
                }
                decodeFrameNum++;
                #if PRINT_PERF
                stepDecodeFrameNum++;
                stepEndTime = std::chrono::steady_clock::now();
                step_diff = stepEndTime - stepStartTime;
                if (step_diff.count() > durationTime) {
                    printPerfStep();
                    stepDecodeFrameNum = 0;
                    stepStartTime = std::chrono::steady_clock::now();
                }
                #endif
            }
        }
        av_packet_unref(&packet);
    }

    return Constat::ok;
}
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
void Decoder::saveImage(std::string filename) {   
    int ret = stbi_write_jpg(filename.c_str(),imgWidth,imgHeight,3,buffer,80);
    if (ret > 0) {
        cout<< "[dump]:save image success, filename: " << filename << ", pts: " << pts << endl;
    } else {
        cout<< "[dump]:save image failed, filename: " << filename << ", pts: " << pts << endl;
    } 
}

std::string Decoder::getImageName() {
    time_t ts = time(NULL);
    std::stringstream ss;
    ss << "/tmp/";
    ss << decodeId;
    ss << "_";
    ss << std::this_thread::get_id();
    ss << "_";
    ss << ts;
    ss << "_";
    ss << decodeFrameNum;
    ss << ".";
    ss << "jpg";
    return ss.str();
}

void Decoder::closeDecode() {

    av_frame_free(&pFrameRGB);
    av_frame_free(&pFrame);
    sws_freeContext(sws_ctx);
    avcodec_close(pCodecCtx);
    avcodec_free_context(&pCodecCtx);

    avformat_close_input(&pFormatCtx);
    av_dict_free(&pAvDict);
}

void Decoder::printPerfTotal() {
    std::chrono::duration<double, std::milli> diff = endTime - startTime;
    actualFps = 0.0f;
    if (diff.count()) {
        actualFps =  (decodeFrameNum * 1000 * 1.f / diff.count());
    }
    // cout << "[FpsStats]: -->thread id: " << std::this_thread::get_id()<<" -->fps: "
    cout << "[FpsStats]: -->thread id: " << threadId <<" -->fps: "
         << std::fixed <<setprecision(3)<< actualFps << " -->total time: " << diff.count();
         cout.unsetf( ios::fixed);
    cout << " -->frame_count: " << decodeFrameNum;
    cout << std::endl;
}

void Decoder::printPerfStep() {
    double stepFps = 0.0f;
    if (step_diff.count()) {
        stepFps =  (stepDecodeFrameNum * 1000 * 1.f / step_diff.count());
    }
    // cout << "[stepFpsStats]: -->thread id: " << std::this_thread::get_id()<<" -->fps: "
    cout << "[stepFpsStats]: -->thread id: " << threadId <<" -->fps: "
         << std::fixed <<setprecision(2)<< stepFps << " -->step time: "
         << step_diff.count() << "ms";
         cout.unsetf( ios::fixed);
    cout << " -->step_count: " << stepDecodeFrameNum << " -->frame_count: " << decodeFrameNum;
    cout << std::endl;
}

double Decoder::getActualFps() {
    if (actualFps < 0) {
        actualFps = 0.0f;
    }
    return actualFps;
}
