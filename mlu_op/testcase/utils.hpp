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
#ifndef __UTILS_H
#define __UTILS_H

#include <iostream>
#include <string.h>
#include <opencv2/opencv.hpp>
#include "cncv.h"

static uint32_t getPixFmtChannelNum(cncvPixelFormat pixfmt) {
  if (pixfmt == CNCV_PIX_FMT_BGR || pixfmt == CNCV_PIX_FMT_RGB) {
    return 3;
  } else if (pixfmt == CNCV_PIX_FMT_ABGR || pixfmt == CNCV_PIX_FMT_ARGB ||
             pixfmt == CNCV_PIX_FMT_BGRA || pixfmt == CNCV_PIX_FMT_RGBA) {
    return 4;
  } else if (pixfmt == CNCV_PIX_FMT_NV12 || pixfmt == CNCV_PIX_FMT_NV21) {
    return 1;
  } else {
    std::cout << "don't suport pixfmt" << std::endl;
    return 0;
  }
}

static uint32_t getSizeOfDepth(cncvDepth_t depth) {
  if (depth == CNCV_DEPTH_8U) {
    return 1;
  } else if (depth == CNCV_DEPTH_16F) {
    return 2;
  } else if (depth == CNCV_DEPTH_32F) {
    return 4;
  }
  return 1;
}

static cncvPixelFormat getCNCVPixFmtFromPixindex(std::string &pix_fmt) {
  if (strncmp(pix_fmt.c_str(), "NV12", 4) == 0 ||
      strncmp(pix_fmt.c_str(), "nv12", 4) == 0) {
        return CNCV_PIX_FMT_NV12;
  } else if (strncmp(pix_fmt.c_str(), "NV21", 4) == 0 ||
             strncmp(pix_fmt.c_str(), "nv21", 4) == 0) {
      return CNCV_PIX_FMT_NV21;
  } else if (strcmp(pix_fmt.c_str(), "RGB24") == 0 ||
             strcmp(pix_fmt.c_str(), "rgb24") == 0) {
        return  CNCV_PIX_FMT_RGB;
  } else if (strcmp(pix_fmt.c_str(), "BGR24") == 0 ||
             strcmp(pix_fmt.c_str(), "bgr24") == 0) {
      return CNCV_PIX_FMT_BGR;
  } else if (strncmp(pix_fmt.c_str(), "ARGB", 4) == 0 ||
             strncmp(pix_fmt.c_str(), "argb", 4) == 0) {
      return CNCV_PIX_FMT_ARGB;
  } else if (strncmp(pix_fmt.c_str(), "ABGR", 4) == 0 ||
             strncmp(pix_fmt.c_str(), "abgr", 4) == 0) {
      return CNCV_PIX_FMT_ABGR;
  } else if (strncmp(pix_fmt.c_str(), "RGBA", 4) == 0 ||
             strncmp(pix_fmt.c_str(), "rgba", 4) == 0) {
      return CNCV_PIX_FMT_RGBA;
  } else if (strncmp(pix_fmt.c_str(), "BGRA", 4) == 0 ||
             strncmp(pix_fmt.c_str(), "bgra", 4) == 0) {
      return CNCV_PIX_FMT_BGRA;
  } else {
      printf("unsupported pixel format\n");
      return CNCV_PIX_FMT_INVALID;
    }
}
static int readRawYUV(const char *filename, uint32_t width, uint32_t height,
               uint8_t **YUV) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    perror("Error opening yuv image for read");
    return 1;
  }

  fseek(fp, 0, SEEK_END);
  uint32_t size = ftell(fp);
  if (size != (width * height + 2 * ((width + 1) / 2) * ((height + 1) / 2))) {
    fprintf(stderr, "Wrong size of yuv image : %d bytes, expected %d bytes\n",
            size,
            (width * height + 2 * ((width + 1) / 2) * ((height + 1) / 2)));
    fclose(fp);
    return 2;
  }
  fseek(fp, 0, SEEK_SET);

  *YUV = (uint8_t*)malloc(size);
  size_t result = fread(*YUV, 1, size, fp);
  if (result != size) {
    perror("Error reading yuv image");
    fclose(fp);
    return 3;
  }
  fclose(fp);
  return 0;
}
static int saveRawYUV(const char *filename, uint32_t width, uint32_t height,
               const uint8_t *YUV, size_t y_stride, size_t uv_stride) {
  FILE *fp = fopen(filename, "wb");
  if (!fp) {
    perror("Error opening yuv image for write");
    return 1;
  }

  if (y_stride == width) {
    fwrite(YUV, 1, width * height, fp);
    YUV += width * height;
  } else {
    for (uint32_t y = 0; y < height; ++y) {
      fwrite(YUV, 1, width, fp);
      YUV += y_stride;
    }
  }

  if (uv_stride == ((width + 1) / 2)) {
    fwrite(YUV, 1, ((width + 1) / 2) * ((height + 1) / 2) * 2, fp);
  } else {
    for (uint32_t y = 0; y < ((height + 1) / 2); ++y) {
      fwrite(YUV, 1, ((width + 1) / 2), fp);
      YUV += uv_stride;
    }

    for (uint32_t y = 0; y < ((height + 1) / 2); ++y) {
      fwrite(YUV, 1, ((width + 1) / 2), fp);
      YUV += uv_stride;
    }
  }

  fclose(fp);
  return 0;
}

static void NV12_TO_RGB24(cv::Mat &src_yuv, cv::Mat &dst_rgb) {
  cv::cvtColor(src_yuv, dst_rgb, cv::COLOR_BGR2YUV_I420);
}
static void NV21_TO_RGB24(cv::Mat &src_yuv, cv::Mat &dst_rgb) {
  cv::cvtColor(src_yuv, dst_rgb, cv::COLOR_YUV2RGB_NV21);
}
static void NV12_TO_BGR24(cv::Mat &src_yuv, cv::Mat &dst_bgr) {
  cv::cvtColor(src_yuv, dst_bgr, cv::COLOR_YUV2BGR_NV12);
}
static void NV21_TO_BGR24(cv::Mat &src_yuv, cv::Mat &dst_bgr) {
  cv::cvtColor(src_yuv, dst_bgr, cv::COLOR_YUV2BGR_NV21);
}
static void BGR24_TO_NV12(cv::Mat &src_bgr, cv::Mat &dst_yuv) {
  cv::Mat img_i420;
  cv::cvtColor(src_bgr, img_i420, cv::COLOR_BGR2YUV_I420);
  cv::Mat img_nv12;
  img_nv12 = cv::Mat(src_bgr.rows * 3/2, src_bgr.cols, CV_8UC1);

  uint8_t *yuv420   = img_i420.ptr<uint8_t>();
  uint8_t *ynv12    = img_nv12.ptr<uint8_t>();
  int32_t uv_height = src_bgr.rows / 2;
  int32_t uv_width  = src_bgr.cols / 2;
  int32_t y_size    = src_bgr.rows * src_bgr.cols;
  memcpy(ynv12, yuv420, y_size);

  uint8_t *nv12 = ynv12 + y_size;
  uint8_t *u_data = yuv420 + y_size;
  uint8_t *v_data = u_data + uv_height * uv_width;

  for (int32_t i = 0; i < uv_width * uv_height; i++) {
      *nv12++ = *u_data++;
      *nv12++ = *v_data++;
  }
  dst_yuv = img_nv12.clone();
}
static void BGR24_TO_NV21(cv::Mat &src_bgr, cv::Mat &dst_yuv) {
  cv::Mat img_i420;
  cv::cvtColor(src_bgr, img_i420, cv::COLOR_BGR2YUV_I420);
  cv::Mat img_nv21;
  img_nv21 = cv::Mat(src_bgr.rows * 3/2, src_bgr.cols, CV_8UC1);

  uint8_t *yuv420   = img_i420.ptr<uint8_t>();
  uint8_t *ynv21    = img_nv21.ptr<uint8_t>();
  int32_t uv_height = src_bgr.rows / 2;
  int32_t uv_width  = src_bgr.cols / 2;
  int32_t y_size    = src_bgr.rows * src_bgr.cols;
  memcpy(ynv21, yuv420, y_size);

  uint8_t *nv21 = ynv21 + y_size;
  uint8_t *v_data = yuv420 + y_size;
  uint8_t *u_data = v_data + uv_height * uv_width;

  for (int32_t i = 0; i < uv_width * uv_height; i++) {
      *nv21++ = *u_data++;
      *nv21++ = *v_data++;
  }
  dst_yuv = img_nv21.clone();
}
static void RGB24_TO_NV12(cv::Mat &src_rgb, cv::Mat &dst_yuv) {
  cv::Mat img_i420;
  cv::cvtColor(src_rgb, img_i420, cv::COLOR_RGB2YUV_I420);
  cv::Mat img_nv12;
  img_nv12 = cv::Mat(src_rgb.rows * 3/2, src_rgb.cols, CV_8UC1);

  uint8_t *yuv420   = img_i420.ptr<uint8_t>();
  uint8_t *ynv12    = img_nv12.ptr<uint8_t>();
  int32_t uv_height = src_rgb.rows / 2;
  int32_t uv_width  = src_rgb.cols / 2;
  int32_t y_size    = src_rgb.rows * src_rgb.cols;
  memcpy(ynv12, yuv420, y_size);

  uint8_t *nv12 = ynv12 + y_size;
  uint8_t *u_data = yuv420 + y_size;
  uint8_t *v_data = u_data + uv_height * uv_width;

  for (int32_t i = 0; i < uv_width * uv_height; i++) {
      *nv12++ = *u_data++;
      *nv12++ = *v_data++;
  }
  dst_yuv = img_nv12.clone();
}
static void RGB24_TO_NV21(cv::Mat &src_rgb, cv::Mat &dst_yuv) {
  cv::Mat img_i420;
  cv::cvtColor(src_rgb, img_i420, cv::COLOR_RGB2YUV_I420);
  cv::Mat img_nv21;
  img_nv21 = cv::Mat(src_rgb.rows * 3/2, src_rgb.cols, CV_8UC1);

  uint8_t *yuv420   = img_i420.ptr<uint8_t>();
  uint8_t *ynv21    = img_nv21.ptr<uint8_t>();
  int32_t uv_height = src_rgb.rows / 2;
  int32_t uv_width  = src_rgb.cols / 2;
  int32_t y_size    = src_rgb.rows * src_rgb.cols;
  memcpy(ynv21, yuv420, y_size);

  uint8_t *nv21 = ynv21 + y_size;
  uint8_t *v_data = yuv420 + y_size;
  uint8_t *u_data = v_data + uv_height * uv_width;

  for (int32_t i = 0; i < uv_width * uv_height; i++) {
      *nv21++ = *u_data++;
      *nv21++ = *v_data++;
  }
  dst_yuv = img_nv21.clone();
}
static void BGRA32_TO_NV12(cv::Mat &src_bgra, cv::Mat &dst_yuv) {
  cv::Mat img_i420;
  cv::cvtColor(src_bgra, img_i420, cv::COLOR_BGRA2YUV_I420);
  cv::Mat img_nv12;
  img_nv12 = cv::Mat(src_bgra.rows * 3/2, src_bgra.cols, CV_8UC1);

  uint8_t *yuv420   = img_i420.ptr<uint8_t>();
  uint8_t *ynv12    = img_nv12.ptr<uint8_t>();
  int32_t uv_height = src_bgra.rows / 2;
  int32_t uv_width  = src_bgra.cols / 2;
  int32_t y_size    = src_bgra.rows * src_bgra.cols;
  memcpy(ynv12, yuv420, y_size);

  uint8_t *nv12 = ynv12 + y_size;
  uint8_t *u_data = yuv420 + y_size;
  uint8_t *v_data = u_data + uv_height * uv_width;

  for (int32_t i = 0; i < uv_width * uv_height; i++) {
      *nv12++ = *u_data++;
      *nv12++ = *v_data++;
  }
  dst_yuv = img_nv12.clone();
}

static void BGRA32_TO_NV21(cv::Mat &src_bgra, cv::Mat &dst_yuv) {
  cv::Mat img_i420;
  cv::cvtColor(src_bgra, img_i420, cv::COLOR_BGRA2YUV_I420);
  cv::Mat img_nv21;
  img_nv21 = cv::Mat(src_bgra.rows * 3/2, src_bgra.cols, CV_8UC1);

  uint8_t *yuv420   = img_i420.ptr<uint8_t>();
  uint8_t *ynv21    = img_nv21.ptr<uint8_t>();
  int32_t uv_height = src_bgra.rows / 2;
  int32_t uv_width  = src_bgra.cols / 2;
  int32_t y_size    = src_bgra.rows * src_bgra.cols;
  memcpy(ynv21, yuv420, y_size);

  uint8_t *nv21 = ynv21 + y_size;
  uint8_t *u_data = yuv420 + y_size;
  uint8_t *v_data = u_data + uv_height * uv_width;

  for (int32_t i = 0; i < uv_width * uv_height; i++) {
      *nv21++ = *v_data++;
      *nv21++ = *u_data++;
  }
  dst_yuv = img_nv21.clone();
}

#endif
