
寒武纪<sup>®</sup> FFmpeg-MLU
====================================

基于寒武纪<sup>®</sup> MLU370硬件平台，寒武纪 FFmpeg-MLU使用纯C接口实现硬件加速的视频编解码。

## 必备条件 ##

- 支持的操作系统如下：
    - Ubuntu
	- Centos
	- Debian
- 寒武纪MLU-370驱动:
    - neuware-mlu370-driver-4.15.13或更高版本
- 寒武纪MLU-370-SDK:
    - cntookit-mlu370-2.6.4-1 (推荐)
- 寒武纪MLU-370-cncv:
    - cncv-0.4.0-1 (不超过0.7.0)

## 补丁、编译FFmpeg-MLU ##

1. 获取FFmpeg源代码并通过下面Git*命令打补丁：

   ```sh
   git clone https://gitee.com/mirrors/ffmpeg.git -b release/4.2 --depth=1
   cd ffmpeg
   git apply ../ffmpeg4.2_mlu_v3.patch
   ```
2. 运行下面命令，通过 ``LD_LIBRARY_PATH`` 设置指定路径：

   ```sh
   export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${NEUWARE_HOME}/lib64
   ```

3. 运行下面命令配置并编译 FFmpeg-MLU:

   ```sh
   ./configure  --enable-gpl \
                --extra-cflags="-I${NEUWARE_HOME}/include" \
                --extra-ldflags="-L${NEUWARE_HOME}/lib64" \
                --extra-libs="-lcncodec_v3 -lcnrt -ldl -lcndrv" \
                --enable-ffplay \
                --enable-ffmpeg \
                --enable-mlumpp \
                --enable-gpl \
                --enable-version3 \
                --enable-nonfree \
                --disable-shared \
                --enable-static \
                --disable-debug \
                --enable-stripping \
                --enable-optimizations

   make -j
   ```

4. 如果想要运行FFmpeg-MLU解码示例，运行下面命令(在examples下提供了hw_decode_mlu解码示例)：
   ```sh
   make -j examples
   ```

5. (可选) 如果想要通过MLU加速实现视频缩放及颜色空间转换等功能，需先安装**MLU-CNCV硬件加速库**(参考具体版本要求), 然后编译 ``mlu_op`` 并拷贝 ``libeasyOP.so`` 到 ``${NEUWARE_HOME}/lib64`` 目录下。
   ```sh
   #1: 安装完成cncv库
    ...
   #2: 编译mlu_op算子
   cd mlu_op & mkdir build
   cd build & cmake .. & make -j
   #3: 拷贝libeasyOP.so 到NEUWARE_HOME路径下
   mv ../lib/libeasyOP.so ${NEUWARE_HOME}/lib64
   ```

## FFmpeg-MLU视频编解码 ##

### 视频解码 ###

FFmpeg-MLU支持的视频解码格式如下：

- H.264/AVC
    - Codec名称：``h264_mludec``
- HEVC
    - Codec名称：``hevc_mludec``
- JPEG
    - Codec名称：``mjpeg_mludec``


**运行示例**

```sh
ffmpeg -y -c:v h264_mludec -i input_file output_file.yuv
ffmpeg -y -hwaccel mlu -hwaccel_output_format mlu -c:v h264_mludec -i input_file -vf hwdownload_mlu output_file.yuv
```
解码器目前支持H2D和H2H的解码数据流程。使用H2D解码时需要在命令行中加上-hwaccel_output_format mlu 来使用硬件加速,解码后数据会存放在Device端。

### 视频编码 ###

FFmpeg-MLU目前支持的编码格式如下：

- H.264/AVC
    - Codec名称：``h264_mluenc``
- HEVC
    - Codec名称：``hevc_mluenc``
- HEVC
    - Codec名称：``mjpeg_mluenc``
**运行示例**

```sh
ffmpeg -i input_file -c:v h264_mluenc <output.h264>
```
单一使用编码器目前支持H2H的编码数据流程。在使用mlu decoder以及filter的情况下支持D2H的编码数据流程。


## 基本测试 ##

### Baseline编码测试 ###

```sh
ffmpeg -benchmark -re -i input.mkv -c:v h264_mluenc -f null -
```
### 缩放解码 ###
```sh
ffmpeg -y -vsync 0 -c:v h264_mludec -i input_1920x1080.mkv -vf scale=320:240 output_320x240.mp4

ffmpeg -y -vsync 0 -c:v h264_mludec -i input_1920x1080.mkv -vf scale_yuv2yuv_mlu=320:240:0 output_320x240.mp4

ffmpeg -y -hwaccel mlu -hwaccel_output_format mlu -c:v h264_mludec -i input_file -vf scale_yuv2yuv_mlu=320:240:0,hwdownload_mlu output_file.yuv
```
### 1:1无缩放转码 ###

ffmpeg -y -vsync 0 -c:v h264_mludec -i fhd_input.mkv -c:a copy -c:v h264_mluenc -b:v 5M output.mp4

ffmpeg -y -hwaccel mlu -hwaccel_output_format mlu -c:v h264_mludec -i input_file -c:v h264_mluenc output_file.h264

### 1:N可缩放转码 ###
```sh
ffmpeg -y -vsync 0 -c:v h264_mludec -i fhd_input.mkv -vf scale=1280:720 -c:a copy -c:v h264_mluenc -b:v 2M output1.mp4 -vf scale=640:360 -c:a copy -c:v h264_mluenc -b:v 512K output2.mp4
```

### 解码 + MLU Filter + 编码 ###
```sh
ffmpeg -y -vsync 0 -c:v h264_mludec -i input_1920x1080.mkv -vf scale_yuv2yuv_mlu=320:240:0 -c:v h264_mluenc output_320x240.h264

ffmpeg -y -vsync 0 -c:v h264_mludec -i input_1920x1080.mkv -vf cvt_yuv2rgbx_mlu=rgb24:0,cvt_rgbx2yuv_mlu=nv12:0 output.h264

ffmpeg -y -hwaccel mlu -hwaccel_output_format mlu -c:v h264_mludec -i input_file -vf scale_yuv2yuv_mlu=320:240:0 -c:v h264_mluenc output_file.h264
```

## 视频质量测试 ##

本节介绍了如何通过PSNR和SSTM方法测试视频质量：

### PSNR ###

```sh
./ffmpeg -i src.h264  -i dst.h264  -lavfi psnr="stats_file=psnr.log" -f null -
```
### SSIM ###

```sh
./ffmpeg -i src.h264  -i dst.h264  -lavfi ssim="stats_file=ssim.log" -f null -
```


## 功能及性能调试 ##

本节介绍了如何使用编解码器的其他功能，及调试改进视频编解码的性能。

### MLU解码器 ###

|选项|类型|描述|
|-|-|-|
|device_id|int|选择使用的加速卡。<br>支持设置的值的范围为：**0** - *INT_MAX*。其中 *INT_MAX* 为加速卡总数减1。 <br>默认值为 **0**。|
|input_buf_num|int|用于解码器输入缓冲器的数量。 <br>支持设置的值的范围为：**1** - **18**。 <br>默认值为 **4**。|
|output_buf_num|int|用于解码器输出缓冲器的数量。 <br>支持设置的值的范围为：**1** - **18**。 <br>默认值为 **4**。|
|stride_align|int|解码器输出对齐的步长。 <br>支持设置的值的范围为：**1** - **128**，可以是 **2^(0 - 7)**。 <br>默认值为 **1**。|
|output_pixfmt|int|输出像素的格式。 <br>支持设置的值为： <br>- **nv12/nv21/p010/yuv420p**。 <br>默认值为 **nv12**。|
|resize|string|调整视频大小(宽)x(高)。 <br>可以设置为 **1/2** 或 **1/4** 缩放视频。 <br>默认为 null。|
|trace|int|FFmpeg-MLU调试开关。<br>支持设置的值为：<br>- **0**: 表示关闭。 <br>- **1**: 表示开启。 <br>默认值为 **0**。|


### MLU硬件加速filter ###

|filter名|功能描述|调用示例|
|-|-|-|
|scale_yuv2yuv_mlu|- 接收 YUV 格式图像（NV12/NV21）<br>- scale到指定大小后输出,<br>- 输出的宽度和高度必须为偶数<br>- 支持图像像素深度为 8u|**-vf filter=<out_w>:<out_h>:<dev_id>**<br>- <out_w>:输出图像宽度 <br>- <ou_h>:输出图像高度 <br>- <dev_id>:设备号|
|scale_rgbx2rgbx_mlu|- 接收RGBX格式图像,scale到指定大小后输出<br>- 要求输入图像的宽高均不超过8192像素<br>- 支持图像像素深度为 8u|**-vf  filter=<out_w>:<out_h>:<dev_id>**<br>- <out_w>:输出图像宽度 <br>- <out_h>:输出图像高度 <br>- <dev_id>:设备号|
|cvt_yuv2rgbx_mlu|- 接收YUV图像(NV12/NV21)<br>- 将图像颜色空间转换为指定RGBX后输出, <br>- 输入图像的宽高均不超过 8192 像素<br>- 输入图像的宽高必须为偶数<br>- 支持图像像素深度为 8u|**-vf filter=<out_fmt>:<dev_id>**<br>- <out_fmt>:输出像素格式<br>- <dev_id>:设备号|
|cvt_rgbx2yuv_mlu|- 接收RGBX图像,<br>- 将图像色彩空间转换为指定的YUV(NV12/NV21), <br>- 输入图像的宽高不小于 2<br>- 输入图像像素格式支持 RGB24、BGR24、RGBA、BGRA、ARGB、ABGR<br>- 输出图像像素格式支持NV12和NV21<br>- 支持图像像素深度为 8U|**-vf filter=<out_fmt>:<dev_id>**<br>- <out_fmt>:输出像素格式<br>- <dev_id>:设备号|
|cvt_rgbx2rgbx_mlu|- 接收RGB图像(ARGB/RGBA/ABGR/BGRA/RGB24/BGR24)<br>- 将一种RGB图像转换为另一种指定RGB后输出, <br>- 输入图像的宽高均不超过 8192 像素<br>- 输入图像的宽高必须为偶数，输入和输出图像RGB格式不可以相同 |**-vf filter=<out_fmt>:<dev_id>**<br>- <out_fmt>:输出像素格式<br>- <dev_id>:设备号|
|scale_cvt_yuv2rgbx_mlu|- 接收YUV图像(NV12/NV21)<br>- 将图像颜色空间转换为指定RGBX系列，再调整到指定大小 <br>- 支持 Gray 格式图像作为输入输出，此格式当前仅实现 resize 功能输入 <br>- 输入图像格式支持 NV12 和 NV21 <br>- 输出图像格式支持 RGB、BGR、RGBA、BGRA、ARGB、ABGR <br>- 输入图像的宽高必须为偶数<br>- 支持图像像素深度为 8U，且输入与输出的数据类型必须相同|**-vf filter=<dst_width>:<dst_height>:<out_fmt>:<dev_id>**<br>- <dst_width>:输出图像宽度<br>- <dst_height>:输出图像高度<br>- <out_fmt>:输出像素格式<br>- <dev_id>:设备号|


### MLU编码器（非MJPEG编码器） ###
这一部分编码器说明包括h264_mluenc以及hevc_mluenc，接受nv12/nv21/yuv420p格式的yuv文件作为编码输入数据。

|选项|类型|描述|
|-|-|-|
|device_id|int|选择使用的加速卡。<br>支持设置的值的范围为：**0** - *INT_MAX*。其中 *INT_MAX* 为加速卡总数减1。 <br>默认值为 **0**。|
|stride_align|int|编码器输出对齐的步长。 <br>支持设置的值的范围为：**1** - **128**，可以是 **2^(0 - 7)**。 <br>默认值为 **1**。|
|rdo_level|int|编码器rdo等级。等级越低，速度越快，质量越低。 <br>支持设置的值为： <br>- **0～4**。 <br>默认值为 **0**。|
|trace|int|FFmpeg-MLU调试开关。<br>支持设置的值为：<br>- **0**: 表示关闭。 <br>- **1**: 表示开启1级别，<br>- **2**: 表示开启2级别。 <br>默认值为 **0**。|
|colorspace|int|编码图像的颜色空间格式。 <br>目前支持设置的值为： <br>- **bt709/bt601**。 <br>默认值为 **bt709**。|
|input_buf_num|int|编码器输入缓存的数量。0代表自动设置，手动设置的数值需要大于frame_interval_p+1。<br>**在使用2pass编码时（lookahead不为0）进行手动设置则需要更大的input_buf_num，所以在2pass编码时推荐将input_buf_num设置为0** <br>支持设置的值的范围为：**0**或者**frame_interval_p + 2** - **INT_MAX**。 <br>默认值为 **0**。|
|stream_buf_size|int|编码器视频流缓存的大小。0代表自动设置。手动设置的数值过小会报错。 <br>支持设置的值的范围为：**0** - **INT_MAX**。 <br>默认值为 **0**。|
|stream_type|int|编码器视频流的类型。 <br>支持设置的值为：**byte_stream/nalu_stream** <br>默认值为 **byte_stream**。|
|frame_interval_p|int|编码视频中两个P帧中可以插入的B帧数量。 <br>支持设置的值的范围为：**0** - *INT_MAX*。 <br>默认值为 **1**。|
|rc|int|编码器的码率控制模式 <br>目前支持设置的值为： <br>- **crf/cbr/vbr/cvbr/fixedqp**。 <br>默认值为 **vbr**。|
|init_qpI|int|fixedqp码流控制模式下的I帧qp。 <br>支持设置的值的范围为：**0** - **51**。 <br>默认值为 **30**。|
|init_qpB|int|fixedqp码流控制模式下的B帧qp。 <br>支持设置的值的范围为：**0** - **51**。 <br>默认值为 **32**。|
|init_qpP|int|fixedqp码流控制模式下的P帧qp。 <br>支持设置的值的范围为：**0** - **51**。 <br>默认值为 **30**。|
|qp|int|码流控制中的qp值。 <br>支持设置的值的范围为：**0** - **51**。 <br>默认值为 **51**。|
|minqp|int|码流控制中的最小qp。 <br>支持设置的值的范围为：**0** - **51**。 <br>默认值为 **0**。|
|maxqp|int|码流控制中的最大qp。 <br>支持设置的值的范围为：**0** - **51**。 <br>默认值为 **51**。|
|profile|int|编码器的profile等级。 <br>HEVC目前支持设置的值的范围为：**main/main_still/prof_max**。H264目前支持的值的范围为：**baseline/main/high** <br>HEVC默认值为 **main**。H264默认值为**main**|
|tier|int| HEVC编码中的tier。H264不支持该属性。 <br>支持设置的值的范围为：**main/high**。 <br>默认值为 **main**。|
|ctb_rc|int|块级码率控制开关，可以开启编码的块级码率控制。 <br>支持设置的值的范围为：**disable/sub/obj/both**。 <br>默认值为 **disable**，即不开启|
|target_quality|int|编码的目标质量，仅在rc设置为crf时生效。 <br>支持设置的值的范围为：**0** - **51**。 <br>默认值为 **0**。|
|lookahead|int|编码时先行深度的大小，开启之后，进行2pass编码，lookahead值越大则画质更好，编码延迟越高。 <br>支持设置的值的范围为：**0** - **40**。在设置为0~3时都视为0，不开启2pass编码。在rc设置为crf时必须开启 <br>默认值为 **0**，不开启|
|block_size|int|块级码控中的宏块颗粒度大小。仅在ctb_rc的值不为disable时生效。<br>支持设置的值的范围为：**64/32/16**。 <br>默认值为 **64**。|
|ctb_row|int|在块级码控中设置行级的qp步长，只在ctb rc为obj和both时生效。 <br>支持设置的值的范围为：**0** - **64**。 <br>默认值为 **0**。|
|rc_qp_delta_range|int|在进行码控时宏块与当前帧的最大qp差值。 <br>支持设置的值的范围为：**0** - **15**。 <br>默认值为 **0**。|
|base_ctb|int|在进行块级码控时的宏块基础复杂度，仅在ctb rc设置为sub以及obj时才能生效。 <br>支持设置的值的范围为：**0** - **30**。 <br>默认值为 **0**。|
|level|int|编码等级控制。 <br>HEVC和H264支持设置的值的范围为：**1/1.0** - **6.2**。 <br>默认值为 **5.1**|

### MLU编码器（MJPEG编码器） ###
这一部分编码器说明为mjpeg_mluenc，H2H的mjpeg编码接受nv12/nv21格式的yuv数据作为编码输入。

|选项|类型|描述|
|-|-|-|
|device_id|int|选择使用的加速卡。<br>支持设置的值的范围为：**0** - *INT_MAX*。其中 *INT_MAX* 为加速卡总数减1。 <br>默认值为 **0**。|
|stride_align|int|编码器输出对齐的步长。 <br>支持设置的值的范围为：**1** - **128**，可以是 **2^(0 - 7)**。 <br>默认值为 **1**。|
|trace|int|FFmpeg-MLU调试开关。<br>支持设置的值为：<br>- **0**: 表示关闭。 <br>- **1**: 表示开启1级别 <br>默认值为 **0**。|
|input_buf_num|int|编码器输入缓存的数量。0代表自动设置，手动设置的数值需要大于frame_interval_p+1。 <br>支持设置的值的范围为：**0**或者**frame_interval_p + 2** - **INT_MAX**。 <br>默认值为 **0**。|
|stream_buf_size|int|编码器视频流缓存的大小。0代表自动设置。手动设置的数值过小会报错。 <br>支持设置的值的范围为：**0** - **INT_MAX**。 <br>默认值为 **0**。|
|quality|int|编码器编码jpeg图片的质量，数值越大编码越慢，质量越高 <br>支持设置的值的范围为：**1** - **100**。 <br>默认值为 **50**。|