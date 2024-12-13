
寒武纪<sup>®</sup> FFmpeg-MLU
====================================

寒武纪<sup>®</sup> MLU硬件平台内置了视频、图像相关的硬件加速编码器和解码器。为了提高计算效率，同时保障产品的可用性和用户使用的便捷性，寒武纪提供了FFmpeg-MLU SDK软件解决方案。FFmpeg-MLU集成了寒武纪硬件加速卡的视频、图像硬件编解码单元和硬件AI计算单元，实现了基于Cambricon MLU硬件加速的视频编码、解码和AI计算；其中硬件视频图像编解码单元基于寒武纪CNCodec加速库开发。依靠FFmpeg音视频编解码和流媒体协议等模块，Cambricon视频、图像编解码单元及AI加速单元可以很便捷的实现高性能硬件加速的多媒体处理pipeline。

​寒武纪 FFmpeg-MLU 使用纯C接口实现硬件加速的图像、视频编解码和常见图像算法处理，完全兼容社区FFmpeg；符合社区FFmpeg代码开发及命令行使用规范，同时也符合社区FFmpeg hwaccel硬件加速框架规范( https://trac.ffmpeg.org/wiki/HWAccelIntro )，实现了硬件内存管理、硬件加速处理模块与cpu模块的流程化兼容处理等。

关于FFmpeg-MLU的更详细介绍请查阅官网或联系对接工作人员。
