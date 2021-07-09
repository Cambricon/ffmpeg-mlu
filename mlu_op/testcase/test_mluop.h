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
#ifndef __TEST_MLUOP_H__
#define __TEST_MLUOP_H__

#include "cnrt.h"
#include "mluop.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>

#define PRINT_TIME 0
#define THREADS_NUM 60

#define CNRT_ERROR_CHECK(ret)                                                  \
  if (ret != CNRT_RET_SUCCESS) {                                               \
    fprintf(stderr, "error occur, func: %s, line: %d\n", __func__, __LINE__);  \
    return 0;                                                                 \
  }

typedef struct {
  int algo;
  bool save_flag;
  uint32_t width;
  uint32_t height;
  uint32_t src_w;
  uint32_t src_h;
  uint32_t dst_w;
  uint32_t dst_h;
  uint32_t frame_num;
  uint32_t thread_num;
  uint32_t depth_size;
  uint32_t pix_chn_num;
  uint32_t device_id;
  const char *pix_fmt;
  const char *depth;
  const char *input_file;
  const char *output_file;

  //yuv cncv resize
  uint32_t src_roi_x;
  uint32_t src_roi_y;

  //rgbx cncv resize
  uint32_t dst_roi_x;
  uint32_t dst_roi_y;

  //yuv2rgbx cncv convert
  const char *src_pix_fmt;
  const char *dst_pix_fmt;

}param_ctx_t;

uint32_t getPixFmtChannelNum(cncvPixelFormat pixfmt);
uint32_t getSizeOfDepth(cncvDepth_t depth);
cncvPixelFormat getCNCVPixFmtFromPixindex(const char* pix_fmt);

int readRawYUV(const char *filename, uint32_t width, uint32_t height,
               uint8_t **YUV);
int saveRawYUV(const char *filename, uint32_t width, uint32_t height,
               const uint8_t *YUV, size_t y_stride, size_t uv_stride);
int set_cnrt_ctx(unsigned int device_id, cnrtChannelType_t channel_id);

#endif /* __TEST_MLUOP_HPP__ */