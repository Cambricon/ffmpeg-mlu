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
#include "cncv.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

#define ALIGN_Y_SCALE 1
#define ALIGN_R_SCALE 1
#define ALIGN_Y2R_CVT 1
#define ALIGN_R2Y_CVT 1
#define PAD_UP(x, y) ((x / y + (int)((x) % y > 0)) * y)

typedef void *HANDLE;

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

/*------------------------resize yuv2yuv cncv----------------------*/
int mluop_resize_yuv_init(HANDLE *h,
                          int input_w, int input_h,
                          int output_w, int output_h,
                          const char *depth, const char *pix_fmt);
int mluop_resize_yuv_exec(HANDLE h,
                          void *input_y, void *input_uv,
                          void *output_y, void *output_uv);
int mluop_resize_pad_yuv_exec(HANDLE h,
                              void *input_y, void *input_uv,
                              void *output_y, void *output_uv);
int mluop_resize_roi_yuv_exec(HANDLE h,
                              void *input_y, void *input_uv,
                              void *output_y, void *output_uv,
                              uint32_t src_roi_x, uint32_t src_roi_y,
                              uint32_t src_roi_w, uint32_t src_roi_h,
                              uint32_t dst_roi_x, uint32_t dst_roi_y,
                              uint32_t dst_roi_w, uint32_t dst_roi_h);
int mluop_resize_yuv_destroy(HANDLE h);

/*------------------------resize rgbx2rgbx cncv----------------------*/
int mluop_resize_rgbx_init(HANDLE *h,
                           int input_w, int input_h,
                           int output_w, int output_h,
                           const char *pix_fmt, const char *depth);
int mluop_resize_rgbx_exec(HANDLE h,
                           void *input, void *output);
int mluop_resize_rgbx_destroy(HANDLE h);

/*------------------------convert yuv2rgbx cncv----------------------*/
int mluop_convert_yuv2rgbx_init(HANDLE *h,
                                int width, int height,
                                const char *src_pix_fmt, const char *dst_pix_fmt,
                                const char *depth);
int mluop_convert_yuv2rgbx_exec(HANDLE h,
                                void *input_y, void *input_uv,
                                void *output);
int mluop_convert_yuv2rgbx_destroy(HANDLE h);

/*------------------------convert rgbx2yuv cncv----------------------*/
int mluop_convert_rgbx2yuv_init(HANDLE *h,
                                int width, int height,
                                const char *src_pix_fmt, const char *dst_pix_fmt,
                                const char *depth);
int mluop_convert_rgbx2yuv_exec(HANDLE h,
                                void *input_rgbx,
                                void *output_y, void *output_uv);
int mluop_convert_rgbx2yuv_destroy(HANDLE h);


#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* __MLUOP_H__ */
