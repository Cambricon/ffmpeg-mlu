#include <iostream>
#include <thread>
#include <vector>
#include <iomanip>
#include <unordered_map>
#include "video_encoder.h"

std::vector<double> fps_stat;
void encode_task(std::string filepath, int width, int height, int frame_num, int deviceid, int thread_num, int picture_write_flag) {
    VideoEncoder Encode;
    if(Encode.initEncoder(filepath, width, height, frame_num, deviceid, thread_num, picture_write_flag) < 0 ) {
        std::cout<< "thread_:"<< thread_num << " init encoder failed!" << std::endl;;
        return;
    }
    if(Encode.encode() < 0) {
        std::cout << "thread_:"<< thread_num << " error while trascoding." << std::endl;
        return;
    }
    Encode.closeEncoder();
    fps_stat.push_back(Encode.getActualFps());
    return;
}

int main(int argc, char** argv) {
    if (argc != 8) {
        std::cout << "Usage: "<< argv[0] <<" <file_path> <pic_width> <pic_height> <pic_num> <device_id> <thread_num> <stream_save_flag>" << std::endl;
        return -1;
    }
    std::string filepath = argv[1];
    int width = atoi(argv[2]);
    int height = atoi(argv[3]);
    int frame_num = atoi(argv[4]);
    int device_id = atoi(argv[5]);
    int thread_num = atoi(argv[6]);
    int picture_save_flag = atoi(argv[7]);
    std::vector<std::thread> vec_th;
    
    for (int i = 0; i < thread_num; i++) {
        vec_th.emplace_back( encode_task, filepath, width, height, frame_num, device_id, i, picture_save_flag);
    }

    for (auto &th:vec_th) {
        if (th.joinable()) {
            th.join();
        }
    }
    double totalAvgFps = 0.0f;
    for (auto iter = fps_stat.begin(); iter != fps_stat.end(); ++iter) {
        totalAvgFps += *iter;
    }
    std::cout << "[FpsStats]: --> " << std::fixed << std::setprecision(3)
         << "Total Fps: " << totalAvgFps << " --> Average Fps: "\
         << totalAvgFps / (atoi(argv[4]) * 1.f) << std::endl;
    return 0;
}
