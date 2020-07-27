中文|[英文](README.md)

寒武纪<sup>®</sup> FFmpeg-MLU
====================================

基于寒武纪<sup>®</sup> MLU硬件平台，寒武纪 FFmpeg-MLU使用纯C接口实现硬件加速的视频编解码。

## 必备条件 ## 

- 支持的操作系统如下： 
    - Ubuntu
	- Centos
	- Debian
- 寒武纪MLU驱动: 
    - neuware-mlu270-driver-4.2.0或更高版本。
- 寒武纪MLU SDK: 
    - neuware-mlu270-1.4.0-1或更高版本。
	
## 补丁、编译FFmpeg-MLU ## 

1. 获取FFmpeg源代码并通过下面Git*命令打补丁：

   ```sh
   git clone https://gitee.com/mirrors/ffmpeg.git -b release/4.2 --depth=1
   cd ffmpeg    
   git apply ../ffmpeg4.2_mlu.patch
   ```
2. 如果使用CentOS操作系统，运行下面命令，通过 ``LD_LIBRARY_PATH`` 设置指定路径：

   ```sh
   export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/neuware/lib64
   ```

3. 运行下面命令配置并创建 FFmpeg-MLU:

   ```sh
   ./configure --enable-gpl \
               --enable-version3 \
               --enable-mlumpp \
               --extra-cflags="-I/usr/local/neuware/include" \
               --extra-ldflags="-L/usr/local/neuware/lib64" \
               --extra-libs="-lcncodec -lcnrt -ldl -lcndrv"
   make -j
   ```
4. 如果想要运行MLU支持多线程的转码示例，运行下面命令：

   ```sh
   make -j examples
   ```

5. (可选) 如果想要通过MLU算子实现视频缩放等功能，编译 ``mluop`` 并拷贝 ``libeasyOP.so`` 到 ``/usr/local/neuware/lib64`` 目录下。

## FFmpeg-MLU视频编解码 ## 

### 视频解码 ###

FFmpeg-MLU支持的视频解码格式如下：

- H.264/AVC 
    - Codec名称：``h264_mludec``
- HEVC
    - Codec名称：``hevc_mludec``
- VP8
    - Codec名称：``vp8_mludec``
- VP9
    - Codec名称：``vp9_mludec``
- JPEG
    - Codec名称：``mjpeg_mludec``
  
**运行示例**

```sh
./ffmpeg -c:v h264_mludec -i input_file -f null -
```
### 视频编码 ###

FFmpeg-MLU支持的视频编码格式如下：

- H.264/AVC
    - Codec名称：``h264_mluenc``
- HEVC
    - Codec名称：``hevc_mluenc``

**运行示例**  

```sh
./ffmpeg -i input_file -c:v h264_mluenc <output.h264>
```
## 基本测试 ##

### Baseline编码测试 ###


```sh
./ffmpeg -benchmark -re -i input.mkv -c:v h264_mluenc -f null -
```
### 缩放解码 ###
```sh
./ffmpeg -y -vsync 0 -c:v h264_mludec -i input_1920x1080.mkv -vf scale=320:240 output_320x240.mp4
```
### 1:1无缩放转码 ###

    ./ffmpeg -y -vsync 0 -c:v h264_mludec -i fhd_input.mkv -c:a copy -c:v h264_mluenc -b:v 5M output.mp4

### 1:N可缩放转码 ###
```sh
./ffmpeg -y -vsync 0 -c:v h264_mludec -i fhd_input.mkv -vf scale=1280:720 -c:a copy -c:v h264_mluenc -b:v 2M output1.mp4 -vf scale=640:360 -c:a copy -c:v h264_mluenc -b:v 512K output2.mp4
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
## 性能调优 ##

本节介绍了如何调试改进视频编解码的性能。

### MLU解码器 ###


|选项|类型|描述|
|-|-|-|
|device_id|int|选择使用的加速卡。<br>支持设置的值的范围为：**0** - *INT_MAX*。其中 *INT_MAX* 为加速卡总数减1。 <br>默认值为 **0**。|
|instance_id|int|选择使用的VPU实例。 <br>支持设置的值为： <br>- 取为 **0** - **INT_MAX** 范围: 表示VPU/JPU实例编号。 <br>- **0**: 表示自动选择。 <br>默认值为 **0**。|
|cnrt_init_flag|int|初始化或销毁FFmpeg的IPU设备。 <br>支持设置的值为：<br>- **0**: 表示销毁设备。 <br>- **1**: 表示初始化设备。 <br>默认值为 **1**。|
|input_buf_num|int|用于解码器输入缓冲器的数量。 <br>支持设置的值的范围为：**1** - **18**。 <br>默认值为 **4**。|
|output_buf_num|int|用于解码器输出缓冲器的数量。 <br>支持设置的值的范围为：**1** - **18**。 <br>默认值为 **3**。|
|stride_align|int|解码器输出对齐的步长。 <br>支持设置的值的范围为：**1** - **128**，可以是 **2^(0 - 7)**。 <br>默认值为 **1**。|
|output_pixfmt|int|输出像素的格式。 <br>支持设置的值为： <br>- **nv12/nv21/p010/i420**。 <br>默认值为 **nv12**。|
|resize|string|调整视频大小 （宽）x（高）。 <br>可以设置为 **1/2** 或 **1/4** 缩放视频。 <br>默认为 null。|
|trace|int|FFmpeg-MLU调试开关。<br>支持设置的值为：<br>- **0**: 表示关闭。 <br>- **1**: 表示开启。 <br>默认值为 **0**。|

### MLU编码器 ###

#### 通用 ###

|选项|类型|描述|
|-|-|-|
|device_id|int|选择使用的加速卡。<br>支持设置的值的范围为：**0** - *INT_MAX*。其中 *INT_MAX* 为加速卡总数减1。<br>默认值为 **0**。|
|instance_id|int|选择使用的VPU实例。<br>支持设置的值为： <br>- 值为 **0** - *INT_MAX* 范围：表示VPU实例编号。 <br>- **0**：表示自动选择。 <br>默认值为 **0**。|
|cnrt_init_flag|int|是否在ffmpeg初始化/销毁cnrt。 <br>支持设置的值为： <br>- **0**：表示外部初始化/销毁。 <br>- **1**：表示由ffmpeg初始化/销毁。 <br>默认值为 **1**。|
|input_buf_num|int|用于编码器输入缓冲的数量。  <br>支持设置的值的范围为：**1** - **18**。 <br>默认值为 **3**。|
|output_buf_num|int|用于编码器输出缓冲的数量。 <br>支持设置的值的范围为：**1** - **18**。 <br>默认值为 **5**。|
|trace|int|FFmpeg-MLU调试信息开关。<br>支持的设置的值为： <br>- **0** 表示关闭调试打印信息。<br>- **1**：表示打开调试打印信息。<br>默认值为 **0**。|
|init_qpP|int|设置P帧初始值为QP。<br>支持设置的值的范围为：**-1** - **51**。 <br>默认值为 **-1**。|
|init_qpI|int|设置I帧初始值为QP。<br>支持设置的值的范围为：**-1** - **51**。 <br>默认值为 **-1**。|
|init_qpB|int|设置B帧初始值为QP。<br>支持设置的值的范围为：**-1** - **51**。 <br>默认值为 **-1**。|
|qp|int|恒定QP控制方法，同FFmpeg cqp。<br>支持设置的值的范围为：**-1** - **51**。 <br>默认值为 **-1**。|
|vbr_minqp|int|可变比特率模式，并提供MinQP，同FFmepg qmin。 <br>支持设置的值的范围为：**0** - **51**。 <br>默认值为 **0**。|
|vbr_maxqp|int|可变比特率模式，并提供MaxQP，同FFmpeg qmax。 <br>支持设置的值的范围为：**0** - **51**。 <br>默认值为 **51**。|

(通用) 支持常规ffmpeg的设置：``-b``, ``-bf``, ``-g``, ``-qmin``, ``-qmax``, 具体意义及值范围请参考ffmpeg官方文档。

#### H264 ###

|选项|类型|描述|
|-|-|-|
|profile|const|设置编码档次。<br>支持设置的值为：**baseline**、**main**、**high**和 **high444p**。 <br>默认值为 **high**。|
|level|const|设置编码级别。<br>支持设置的值为：值为 **1** - **5.1** 范围、**auto** 。 <br>默认值为 **4.2**。|
|coder|const|设置编码熵（entropy）模式。<br>支持设置的值为：**cabac** 和 **cavlc**。 <br>默认值为 **cavlc**。|

#### HEVC ###

|选项|类型|描述|
|-|-|-|
|profile|const|设置编码档次。<br>支持设置的值为： **main**、**main_still**、**main_intra**和 **main10**。 <br>默认值为 **main**。|
|level|const|设置编码级别。<br>支持设置的值为：值为 **1** - **6.2** 范围、**auto** 。 <br>默认值为 **1**。|