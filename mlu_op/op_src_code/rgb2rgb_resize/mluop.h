/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
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

typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;
}cnRoi;

/*------------------------resize rgbx2rgbx invoke----------------------*/
int mluop_resize_rgbx_invoke_init(HANDLE *h,
                                  int input_w, int input_h, int output_w, int output_h,
                                  cnPixelFormat pix_fmt, cnDepth_t depth, int device_id,
                                  cnrtChannelType_t channel_id);

int mluop_resize_rgbx_invoke_exec(HANDLE h, void *input, void *src_rois_mlu, void *output,
                                 uint32_t d_x, uint32_t d_y, uint32_t d_w, uint32_t d_h);
int mluop_resize_rgbx_invoke_destroy(HANDLE h);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* __MLUOP_H__ */