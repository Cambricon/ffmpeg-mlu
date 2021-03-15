/*************************************************************************
 * Copyright (C) [2019-2022] by Cambricon, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *************************************************************************/
#ifndef __MLUOP_H__
#define __MLUOP_H__

#include "cnrt.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

typedef void *HANDLE;

typedef enum {
  CN_PIX_FMT_NONE = -1,
  CN_PIX_FMT_NV12 = 0,
  CN_PIX_FMT_NV21 = 1,
  CN_PIX_FMT_I420 = 2,
  CN_PIX_FMT_YUYV = 3,
  CN_PIX_FMT_UYVY = 4,
  CN_PIX_FMT_YVYU = 5,
  CN_PIX_FMT_VYUY = 6,
  CN_PIX_FMT_P010 = 7,
  CN_PIX_FMT_RGB  = 8,
  CN_PIX_FMT_BGR  = 9,
  CN_PIX_FMT_ARGB =10,
  CN_PIX_FMT_ABGR =11,
  CN_PIX_FMT_RGBA =12,
  CN_PIX_FMT_BGRA =13,
  CN_PIX_FMT_GRAY =14,
}cnPixelFormat;

typedef enum {
  CN_DEPTH_NONE = -1,
  CN_DEPTH_8U  = 0,
  CN_DEPTH_8S  = 1,
  CN_DEPTH_16U  = 2,
  CN_DEPTH_16S  = 3,
  CN_DEPTH_32U  = 4,
  CN_DEPTH_32S  = 5,
  CN_DEPTH_16F  = 6,
  CN_DEPTH_32F  = 7,
}cnDepth_t;

typedef enum {
  CN_COLOR_SPACE_BT_601  = 0,
  CN_COLOR_SPACE_BT_709  = 1,
}cnColorSpace;

typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;
}cnRoi;


/*------------------------resize yuv2yuv invoke----------------------*/
int mluop_resize_yuv_invoke_init(HANDLE *h, int input_w, int input_h,
                          int input_stride_in_bytes, int output_w, int output_h,
                          int output_stride_in_bytes, int device_id);
int mluop_resize_yuv_invoke_exec(HANDLE h, void *input_y, void *input_uv,
                          void *output_y, void *output_uv);
int mluop_resize_yuv_invoke_destroy(HANDLE h);

/*------------------------resize rgbx2rgbx invoke----------------------*/
int mluop_resize_rgbx_invoke_init(HANDLE *h, int input_w, int input_h, int output_w,
                                  int output_h, cnPixelFormat pix_fmt, cnDepth_t depth);

int mluop_resize_rgbx_invoke_exec(HANDLE h, void *input, void *output,
                                  uint32_t d_x, uint32_t d_y, uint32_t d_w, uint32_t d_h);
int mluop_resize_rgbx_invoke_destroy(HANDLE h);

/*----------------------------yuv2rgb invoke-------------------------*/
int mluop_convert_yuv2rgb_invoke_init(HANDLE *h, int width, int height, 
                                      cnPixelFormat src_pix_fmt, cnPixelFormat dst_pix_fmt, 
                                      cnDepth_t depth);
int mluop_convert_yuv2rgb_invoke_exec(HANDLE h,
                                      void *input_y, void *input_uv, void *output);
int mluop_convert_yuv2rgb_invoke_destroy(HANDLE h);

/*----------------------------infer sr-------------------------*/
int mluop_infer_sr_init(HANDLE* handle, const char* model_path, int dev_id, int dev_channel);
int mluop_infer_sr_exec(HANDLE handle, void *data_in, uint32_t in_linesize,
                        uint32_t out_linesize, void *data_out);
int mluop_infer_sr_destroy(void* handle);


#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* __MLUOP_H__ */