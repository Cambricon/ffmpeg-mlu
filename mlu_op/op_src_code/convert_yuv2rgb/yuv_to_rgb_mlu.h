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
#ifndef KERNELS_CVT_COLOR_YUV_TO_RGB_YUV_TO_RGB_MLU_H_
#define KERNELS_CVT_COLOR_YUV_TO_RGB_YUV_TO_RGB_MLU_H_

#define MAX_BUFFER_LEN 240 * 1024
#define MAX_KERNEL_LEN 64 * 64 * 4
#include "kernel.h"
#include "mlu.h"

__mlu_global__ void MLUUnion1KernelYuv420spToRgb(uint8_t **src_gdram,
                                  uint8_t **dst_gdram,
                                  int16_t *conv_kernel_gdram,
                                  float *bias_gdram,
                                  const uint32_t height,
                                  const uint32_t width,
                                  const uint32_t src_y_stride,
                                  const uint32_t src_uv_stride,
                                  const uint32_t dst_stride,
                                  const uint32_t batch_size,
                                  const uint32_t ci,
                                  const uint32_t out_chn);

#endif  // KERNELS_CVT_COLOR_YUV_TO_RGB_YUV_TO_RGB_MLU_H_
