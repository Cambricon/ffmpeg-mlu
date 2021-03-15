#include <iostream>
#include <thread>
#include <vector>
#include<iomanip>
#include <unordered_map>
#include "decoder.h"

using namespace std;
std::vector<double> fps_stat;
/**
 * ./decoder frame_num filename dev_id dump_flag thread_num
 *
 * @param argc
 * @param argv
 * @return
 */
void decoder_task(std::string filename,int dev_id, int thread_id, int dump_flag) {
    Decoder d;
    d.decode(filename, dev_id, thread_id, dump_flag);
    d.printPerfTotal();
    fps_stat.push_back(d.getActualFps());
}
int main(int argc, char** argv) {

    if (argc <= 5 && atoi(argv[4]) <=0 ) {
        cout << "Usage: "<< argv[0] <<" <input file> <device_id> <dump flag> <thread_num>" << endl;
        return 0;
    }
    std::vector<std::thread> vec;
    std::string input_file = argv[1];
    int device_id = atoi(argv[2]);
    int dump_flag = atoi(argv[3]);
    int thread_num = atoi(argv[4]);
    for (int i = 0; i < thread_num; i++) {
        vec.push_back(std::thread(decoder_task, input_file, device_id, i, dump_flag));
    }
    for (auto iter = vec.begin(); iter != vec.end(); ++iter) {
        if (iter->joinable()) {
            iter->join();
        }
    }    
    double totalAvgFps = 0.0f;
    for (auto iter = fps_stat.begin(); iter != fps_stat.end(); ++iter){
        totalAvgFps += *iter;
    }
    cout << "[TotalFpsStats]: --> " << std::fixed << setprecision(3)
         << "Total Fps: " << totalAvgFps << " --> Average Fps: "\
         << totalAvgFps / (atoi(argv[4]) * 1.f) << endl;
         cout.unsetf( ios::fixed);
    return 0;
}
