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

#include "mluop.h"
#include "mluop_list.h"
#include "test_mluop.h"
#include <iostream>

uint32_t getPixFmtChannelNum(cnPixelFormat pixfmt) {
  if (pixfmt == CN_PIX_FMT_BGR || pixfmt == CN_PIX_FMT_RGB) {
    return 3;
  } else if (pixfmt == CN_PIX_FMT_ABGR || pixfmt == CN_PIX_FMT_ARGB ||
             pixfmt == CN_PIX_FMT_BGRA || pixfmt == CN_PIX_FMT_RGBA) {
    return 4;
  } else if (pixfmt == CN_PIX_FMT_NV12 || pixfmt == CN_PIX_FMT_NV21) {
    return 1;
  } else {
    std::cout << "don't suport pixfmt" << std::endl;
    return 0;
  }
}

uint32_t getSizeOfDepth(cnDepth_t depth) {
  if (depth == CN_DEPTH_8U) {
    return 1;
  } else if (depth == CN_DEPTH_16F) {
    return 2;
  } else if (depth == CN_DEPTH_32F) {
    return 4;
  }
  return 1;
}

cnPixelFormat getCNPixFmtFromPixindex(const char* pix_fmt) {
  if (strncmp(pix_fmt, "NV12", 4) == 0 ||
      strncmp(pix_fmt, "nv12", 4) == 0) {
        return CN_PIX_FMT_NV12;
  } else if (strncmp(pix_fmt, "NV21", 4) == 0 ||
             strncmp(pix_fmt, "nv21", 4) == 0) {
      return CN_PIX_FMT_NV21;
  } else if (strncmp(pix_fmt, "RGB", 3) == 0 ||
             strncmp(pix_fmt, "rgb", 3) == 0) {
      return CN_PIX_FMT_RGB;
  } else if (strncmp(pix_fmt, "BGR", 3) == 0 ||
             strncmp(pix_fmt, "bgr", 3) == 0) {
      return CN_PIX_FMT_BGR;
  } else if (strncmp(pix_fmt, "ARGB", 4) == 0 ||
             strncmp(pix_fmt, "argb", 4) == 0) {
      return CN_PIX_FMT_ARGB;
  } else if (strncmp(pix_fmt, "ABGR", 4) == 0 ||
             strncmp(pix_fmt, "abgr", 4) == 0) {
      return CN_PIX_FMT_ABGR;
  } else if (strncmp(pix_fmt, "RGBA", 4) == 0 ||
             strncmp(pix_fmt, "rgba", 4) == 0) {
      return CN_PIX_FMT_RGBA;
  } else if (strncmp(pix_fmt, "BGRA", 4) == 0 ||
             strncmp(pix_fmt, "bgra", 4) == 0) {
      return CN_PIX_FMT_BGRA;
  } else {
      printf("unsupported pixel format\n");
      return CN_PIX_FMT_NONE;
    }
}

cnDepth_t getSizeFromDepth(uint32_t depth) {
  if (1 == depth) {
    return CN_DEPTH_8U;
  } else if (2 == depth) {
    return CN_DEPTH_16F;
  } else if (4 == depth) {
    return CN_DEPTH_32F;
  } else {
    std::cout << "unsupport data depth, defalut unit8" << std::endl;
    return CN_DEPTH_8U;
  }
}