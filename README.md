EN|[CN](README_cn.md)

Cambricon<sup>®</sup> FFmpeg-MLU
====================================

Cambricon<sup>®</sup> FFmpeg-MLU supports hardware-accelerated video decoding and encoding using a plain C API on Cambricon MLU hardware platforms.

## Requirements ## 

- Supported OS: 
    - Ubuntu
	- Centos
	- Debian
- Cambricon MLU Driver: 
    - neuware-mlu270-driver-4.2.0 or later.
- Cambricon MLU SDK: 
    - neuware-mlu270-1.4.0-1 or later.

## Patch and Build FFmpeg-MLU ## 

1. Get FFmpeg sources and patch with the following Git* command:

   ```sh
   git clone https://gitee.com/mirrors/ffmpeg.git -b release/4.2 --depth=1
   cd ffmpeg    
   git apply ../ffmpeg4.2_mlu.patch
   ```
2. If you are using CentOS, you need to set ``LD_LIBRARY_PATH`` to the proper directory by running the following commands:

   ```sh
   export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/neuware/lib64
   ```

3. Run the following commands to configure and build FFmpeg-MLU:

   ```sh
   ./configure --enable-gpl \
               --enable-version3 \
               --enable-mlumpp \
               --extra-cflags="-I/usr/local/neuware/include" \
               --extra-ldflags="-L/usr/local/neuware/lib64" \
               --extra-libs="-lcncodec -lcnrt -ldl -lcndrv"
   make -j
   ```
4. (Notice) If you need a MLU transcode demo with multi-threads, run the following commands:

   ```sh
   make -j examples
   ```

5. (Optional) If you need support downscaling with MLU operators, build ``mluop`` and copy ``libeasyOP.so``  to the ``/usr/local/neuware/lib64`` folder.

## Decoding and Encoding with FFmpeg-MLU ## 

### Decoding ###

Supported video decoding formats are as follows:

- H.264/AVC 
    - Codec Name: ``h264_mludec``
- HEVC
    - Codec Name: ``hevc_mludec``
- VP8
    - Codec Name: ``vp8_mludec``
- VP9
    - Codec Name: ``vp9_mludec``
- JPEG
    - Codec Name: ``mjpeg_mludec``
  
**Example**

```sh
./ffmpeg -c:v h264_mludec -i input_file -f null -
```
### Encoding ###

Supported video encoding formats are as follows:

- H.264/AVC
    - Codec Name: ``h264_mluenc``
- HEVC
    - Codec Name: ``hevc_mluenc``

**Example**  

```sh
./ffmpeg -i input_file -c:v h264_mluenc <output.h264>
```
## Basic Testing ##

### Encode Baseline Test ###


```sh
./ffmpeg -benchmark -re -i input.mkv -c:v h264_mluenc -f null -
```
### Decode with Scaling ###
```sh
./ffmpeg -y -vsync 0 -c:v h264_mludec -i input_1920x1080.mkv -vf scale=320:240 output_320x240.mp4
```
### 1:1 Transcode Without Scaling ###

    ./ffmpeg -y -vsync 0 -c:v h264_mludec -i fhd_input.mkv -c:a copy -c:v h264_mluenc -b:v 5M output.mp4

### 1:N Transcode With Scaling ###
```sh
./ffmpeg -y -vsync 0 -c:v h264_mludec -i fhd_input.mkv -vf scale=1280:720 -c:a copy -c:v h264_mluenc -b:v 2M output1.mp4 -vf scale=640:360 -c:a copy -c:v h264_mluenc -b:v 512K output2.mp4
```
## Quality Testing ##

This section introduces how to test video quality with PSNR and SSTM.

### PSNR ###

```sh
./ffmpeg -i src.h264  -i dst.h264  -lavfi psnr="stats_file=psnr.log" -f null -
```
### SSIM ###

```sh
./ffmpeg -i src.h264  -i dst.h264  -lavfi ssim="stats_file=ssim.log" -f null -
```
## Performance Fine-Tune ##

This section introduces how to improve the performance.

### MLU Decoder ###


|Option name|Type|Description|
|-|-|-|
|device_id|int|Select the accelerator card. <br>Supported values range from **0** to *INT_MAX*. *INT_MAX* is the total number of accelerator cards minus 1. <br>The default value is **0**.|
|instance_id|int|Select the VPU instance. <br>Supported values are: <br>- Value in the range **0** - **INT_MAX**: Represents VPU instance. <br>- **0**: The VPU instance is auto-selected. <br>The default value is **0**.|
|cnrt_init_flag|int|Initialize or destory cnrt context in FFmpeg. <br>Supported values are: <br>- **0**: Represents disabled. <br>- **1**: Represents enabled. <br>The default value is **1**.|
|input_buf_num|int|Number of input buffers for decoder. <br>Supported values range from **1** to **18**. <br>The default value is **4**.|
|output_buf_num|int|Number of output buffers for decoder. <br>Supported values range from **1** to **18**. <br>The default value is **3**.|
|stride_align|int|Stride align of output buffers for decoder. <br>Supported values range from **1** to **128**, can be **2^(0 - 7)**. <br>The default value is **1**.|
|output_pixfmt|int|The output pixel format. <br>Supported values are: <br>- **nv12/nv21/p010/i420**. <br>The default value is **nv12**.|
|resize|string|Resize (width)x(height). <br>Only supports **1/2** and **1/4** for down scaling. <br>The default is null.|
|trace|int|MLU FFmpeg MLU trace switch. <br>Supported values are: <br>- **0**: Represents disabled. <br>- **1**: Represents enabled. <br>The default value is **0**.|

### MLU Encoder ###

#### Common ###

|Option name|Type|Description|
|-|-|-|
|device_id|int|Select the accelerator card. <br>Supported values range from **0** to *INT_MAX*. *INT_MAX* is the total number of accelerator cards minus 1. <br>The default value is **0**.|
|instance_id|int|Select the VPU instance. <br>Supported values are: <br>- Value in the range **0** - *INT_MAX*: Represents VPU instance. <br>- **0**: The VPU instance is auto-selected. <br>The default value is **0**.|
|cnrt_init_flag|int|Initialize or destory cnrt context in FFmpeg. <br>Supported values are: <br>- **0**: Represents disabled. <br>- **1**: Represents enabled. <br>The default value is **1**.|
|input_buf_num|int|Number of input buffers for encoder. <br>Supported values range from **1** to **18**. <br>The default value is **3**.|
|output_buf_num|int|Number of output buffers for encoder. <br>Supported values range from **1** to **18**. <br>The default value is **5**.|
|trace|int|MLU FFmpeg debug switch. <br>Supported values are: <br>- **0** Represents disabled.<br>- **1**: Represents enabled. <br>The default value is **0**.|
|init_qpP|int|Initial QP value for P frame, set P frame QP. <br>Supported values range from **-1** to **51**. <br>The default value is **-1**.|
|init_qpI|int|Initial QP value for I frame, set I frame QP. <br>Supported values range from **-1** to **51**. <br>The default value is **-1**.|
|init_qpB|int|Initial QP value for B frame, set B frame QP. <br>Supported values range from **-1** to **51**. <br>The default value is **-1**.|
|qp|int|Constant QP rate control method, same as FFmpeg cqp. <br>Supported values range from **-1** to **51**. <br>The default value is **-1**.|
|vbr_minqp|int|Variable bitrate mode with MinQP, same as FFmepg qmin. <br>Supported values range from **0** to **51**. <br>The default value is **0**.|
|vbr_maxqp|int|Variable bitrate mode with MaxQP, same as FFmpeg qmax. <br>Supported values range from **0** to **51**. <br>The default value is **51**.|

(Notice) Also supports regular ffmpeg settings,such as ``-b``, ``-bf``, ``-g``, ``-qmin``, ``-qmax``, please refers to ffmpeg official documents。

#### H264 ###

|Option name|Type|Description|
|-|-|-|
|profile|const|Set the encoding profile. <br>Supported values are: **baseline**, **main**, **high**, and **high444p**. <br>The default value is **high**.|
|level|const|Set the encoding level restriction. <br>Supported values are: in the range **1** - **5.1**, or **auto**. <br>The default value is **4.2**.|
|coder|const|Set the encoding entropy mode. <br>Supported values are: **cabac** and **cavlc**. <br>The default value is **cavlc**.|

#### HEVC ###

|Option name|Type|Description|
|-|-|-|
|profile|const|Set the encoding profile. <br>Supported values are: **main**, **main_still**, **main_intra**, and **main10**. <br>The default value is **main**.|
|level|const|Set the encoding level restriction. <br>Supported values are: in the range **1** - **6.2**, or **auto**. <br>The default value is **1**.|