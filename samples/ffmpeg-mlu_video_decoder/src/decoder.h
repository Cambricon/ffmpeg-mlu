#ifndef _DECODE_H
#define _DECODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

class Decoder {

public:
    void decode(std::string, int, int, bool);
    void printPerfTotal();
    void printPerfStep();
    double getActualFps();
private:
    int initDecode(std::string);
    int findVideoStreamIndex();
    int decoding();
    std::string getImageName();
    void saveImage(std::string);
    void closeDecode();

private:

    int videoStream = -1;
    AVDictionary *pAvDict = nullptr;
    AVFormatContext *pFormatCtx = nullptr;
    AVCodecContext *pCodecCtx = nullptr;
    SwsContext *sws_ctx = nullptr;

    AVFrame *pFrame = nullptr;
    AVFrame *pFrameRGB = nullptr;
    uint8_t *buffer = nullptr;

    int imgWidth = 0;
    int imgHeight = 0;
    int imgSize = 0;
    int decodeFps = 1;
    int skip = 0;
    int pts = 0;

    int decodeId = 0;
    int decodeCount = 0;
    int decodeFrameNum = 0;
    int stepDecodeFrameNum = 0;
	int devId;
    int threadId;
    bool isDumpFile;
    std::chrono::time_point<std::chrono::steady_clock> startTime;
    std::chrono::time_point<std::chrono::steady_clock> endTime;

    std::chrono::time_point<std::chrono::steady_clock> stepStartTime;
    std::chrono::time_point<std::chrono::steady_clock> stepEndTime;
    std::chrono::duration<double, std::milli> step_diff;;
    double actualFps = 0.0;
    double durationTime = 0.0;
};

enum Constat {
    ok = 0,
    system_error = -1
};

#endif //_DECODE_H
