#ifndef _VIDEO_ENCODER_H
#define _VIDEO_ENCODER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#ifdef __cplusplus
}
#endif

#include <vector>
#include <string>

typedef struct EncoderContext{
        /**
         * Input or output context format.
         */
        AVFormatContext *formatContext;

        /**
         * Decoding/encoding context for the decoder or encoder.
         */
        AVCodecContext *codecContext;

        /**
         * Encoder or decoder.
         */
        AVCodec *codec;

        /**
         * Video data stream.
         */
        AVStream *videoStream;

        // EncoderContext(){};

} EncoderContext;

class VideoEncoder {

public:
    int initEncoder(std::string source, int width, int height, int frame_num, int device_id, int threadid, int picture_write_flag);
    int encode();
    void closeEncoder();
    void printPerf();
    double getActualFps();
    void printPerfStep();
    
private:
    std::string getOutputName();
    int streamWrite(std::string);

private:

    int videoStream = -1;
    AVDictionary *dAVdict = nullptr;
    EncoderContext decodeContext;
    EncoderContext encodeContext;
    AVDictionary *eAVdict = nullptr;
    SwsContext *sws_ctx = nullptr;
    AVPacket *dPacket;
    AVPacket *ePacket;

    AVFrame *pFrame = nullptr;
    AVFrame *pFrameRGB = nullptr;
    uint8_t *buffer = nullptr;

    int imgWidth = 0;
    int imgHeight = 0;
    int imgSize = 0;

    int threadId = 0;
    int encodeCount = 0;
    int encodeFrameNum = 0;
    int stepEncodeFrameNum = 0;
	  int devId;
    int write_flag = 0;

    FILE* input_file;
    FILE* output_file;

    double encodeFps = 0.0f;
    double durationTime = 0.0;
    std::vector< std::chrono::duration<double, std::milli> > encodeTime;
    std::chrono::time_point<std::chrono::steady_clock> encodeStartTime;
    std::chrono::time_point<std::chrono::steady_clock> encodeEndTime;
    std::chrono::time_point<std::chrono::steady_clock> stepStartTime;
    std::chrono::time_point<std::chrono::steady_clock> stepEndTime;
    std::chrono::duration<double, std::milli> step_diff;;
};

enum Constat {
    ok = 0,
    system_error = -1
};

#endif //_VIDEO_ENCODER_H