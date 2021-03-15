# MLU Video to Video encoder

## Prerequisites

- Ubuntu 16.04 or more
- GCC (gcc and g++) ver. 5.4 or later
- Cambricon MLU Driver (v1.3.0 or later)
- Cambricon MLU SDK (v1.3.0 or later)

## Getting Started

### Link Dynamic Library
*Method1:this method is used without soft link*

1. go into the dir *ffmpeg-mlu_video_encoder* and mkdir *3rdparty/ffmpeg*

```bash
cd ffmpeg-mlu_video_encoder
mkdir 3rdparty/ffmpeg
```

2. move or copy FFmpeg *include/* and *lib/* under the dir *3rdparty/ffmpeg*, attention please ,do remember to rename your folders from *include* to *ffmpeg_include*, from *lib* to *ffmpeg_lib*

3. export neuware and ffmpeg dynamic library file path in env LD_LIBRARY_PATH
   **if you did the step 1, the ffmpeg lib path should be <ffmpeg-mlu_video_encoder PATH>/3rdparty/ffmpeg/ffmpeg_lib**

```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/neuware/lib64:<your ffmpeg lib path>
```
*Method2:this method is used with soft link*

1. after running the compile scrift in dir *FFMPEG-MLU-REL*, go to the dir *ffmpeg-mlu_video_encoder/3rdparty/ffmpeg* , you will find four folders named *ffmpeg_include*、*ffmpeg_lib*、*include* and *lib*

2. export neuware and ffmpeg dynamic library file path in env LD_LIBRARY_PATH

    **if you did the step 1, the ffmpeg lib path should be <ffmpeg-mlu_video_encoder PATH>/3rdparty/ffmpeg/ffmpeg_lib, also, if you need external libs, please add <ffmpeg-mlu_video_encoder PATH>/3rdparty/ffmpeg/lib to LD_LIBRARY_PATH**
```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/neuware/lib64:<your ffmpeg lib path>:<your external lib path>
```

### Build

```bash
mkdir build
cd build
cmake ..
make -j
```

### Run

The ffmpeg-mlu_video_encoder need 7 params, <file_path> <pic_width> <pic_height> <pic_num> <device_id> <thread_num> <save_flag>

```bash
./video_encoder <file_path> <pic_width> <pic_height> <pic_num> <device_id> <thread_num> <save_flag>
```
