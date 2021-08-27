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
#include <chrono>
#include <ctime>
#include <deque>
#include <iostream>
#include <string>
#include <memory>
#include <utility>
#include <sys/time.h>

#include "cnrt.h"
#include "mluop.h"

using std::string;
using std::to_string;

#define PRINT_TIME 0

extern cncvStatus_t cncvRgbxToYuv(cncvHandle_t handle,
                                  const cncvImageDescriptor src_desc,
                                  const cncvRect src_roi,
                                  const void *src,
                                  const cncvImageDescriptor dst_desc,
                                  void **dst);

cncvStatus_t GetChannelPosition(const cncvPixelFormat src_pixfmt,
                                const cncvPixelFormat dst_pixfmt,
                                uint32_t &pos_r,
                                uint32_t &pos_g,
                                uint32_t &pos_b,
                                uint32_t &pos_u,
                                uint32_t &pos_v) {

  switch (src_pixfmt) {
    case CNCV_PIX_FMT_RGB:
    case CNCV_PIX_FMT_RGBA: {
      pos_r = 0; pos_g = 1; pos_b = 2;
    }; break;
    case CNCV_PIX_FMT_BGR:
    case CNCV_PIX_FMT_BGRA: {
      pos_r = 2; pos_g = 1; pos_b = 0;
    }; break;
    case CNCV_PIX_FMT_ARGB: {
      pos_r = 1; pos_g = 2; pos_b = 3;
    }; break;
    case CNCV_PIX_FMT_ABGR: {
      pos_r = 3; pos_g = 2; pos_b = 1;
    }; break;
    default: {
      return CNCV_STATUS_BAD_PARAM;
    }
  }

  switch (dst_pixfmt) {
    case CNCV_PIX_FMT_NV12: {
      pos_u = 0; pos_v = 1;
    }; break;
    case CNCV_PIX_FMT_NV21: {
      pos_u = 1; pos_v = 0;
    }; break;
    default: {
      return CNCV_STATUS_BAD_PARAM;
    }
  }
  return CNCV_STATUS_SUCCESS;
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

static cncvDepth_t getCNCVDepthFromIndex(const char* depth) {
  if (strncmp(depth, "8U", 2) == 0 ||
      strncmp(depth, "8u", 2) == 0) {
        return CNCV_DEPTH_8U;
  } else if (strncmp(depth, "16F", 3) == 0 ||
             strncmp(depth, "16f", 3) == 0) {
      return CNCV_DEPTH_16F;
  } else if (strncmp(depth, "32F", 3) == 0 ||
             strncmp(depth, "32f", 3) == 0) {
      return CNCV_DEPTH_32F;
  } else {
      printf("unsupported depth\n");
      return CNCV_DEPTH_INVALID;
    }
}

static cncvPixelFormat getCNCVPixFmtFromPixindex(const char* pix_fmt) {
  if (strcmp(pix_fmt , "NV12") == 0 ||
      strcmp(pix_fmt , "nv12") == 0) {
        return  CNCV_PIX_FMT_NV12;
  } else if (strcmp(pix_fmt , "NV21") == 0 ||
             strcmp(pix_fmt , "nv21") == 0) {
        return  CNCV_PIX_FMT_NV21;
  } else if (strcmp(pix_fmt , "RGB24") == 0 ||
             strcmp(pix_fmt , "rgb24") == 0) {
        return  CNCV_PIX_FMT_RGB;
  } else if (strcmp(pix_fmt , "BGR24") == 0 ||
             strcmp(pix_fmt , "bgr24") == 0) {
        return  CNCV_PIX_FMT_BGR;
  } else if (strcmp(pix_fmt , "ARGB") == 0 ||
             strcmp(pix_fmt , "argb") == 0) {
        return  CNCV_PIX_FMT_ARGB;
  } else if (strcmp(pix_fmt , "ABGR") == 0 ||
             strcmp(pix_fmt , "abgr") == 0) {
        return  CNCV_PIX_FMT_ABGR;
  } else if (strcmp(pix_fmt , "RGBA") == 0 ||
             strcmp(pix_fmt , "rgba") == 0) {
        return  CNCV_PIX_FMT_RGBA;
  } else if (strcmp(pix_fmt , "BGRA") == 0 ||
             strcmp(pix_fmt , "bgra") == 0) {
        return  CNCV_PIX_FMT_BGRA;
  } else {
        printf("unsupported pixel format\n");
        return  CNCV_PIX_FMT_INVALID;
  }
}

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

struct CVRGBX2YUVPrivate {
 public:
  cnrtQueue_t queue_ = nullptr;
  cncvHandle_t handle;
  uint32_t width, height;
  uint32_t src_stride[1], dst_stride[2];
  cncvPixelFormat src_pix_fmt, dst_pix_fmt;
  cncvColorSpace src_color_space = CNCV_COLOR_SPACE_BT_601;
  cncvColorSpace dst_color_space = CNCV_COLOR_SPACE_BT_601;
  cncvDepth_t depth;

  std::deque<std::pair<void*, void*>> dst_yuv_ptrs_cache_;
  void **dst_yuv_ptrs_cpu_ = nullptr;
  void **dst_yuv_ptrs_mlu_ = nullptr;

};  // CVResziePrivate

// according to handle(typedef void* handle) to deliver struct message
int mluop_convert_rgbx2yuv_init(HANDLE *h,
                                int width, int height,
                                const char *src_pix_fmt, const char *dst_pix_fmt,
                                const char *depth) {
  CVRGBX2YUVPrivate *d_ptr_ = new CVRGBX2YUVPrivate;
  cnrtCreateQueue(&d_ptr_->queue_);
  cncvCreate(&d_ptr_->handle);
  cncvSetQueue(d_ptr_->handle, d_ptr_->queue_);

  cnrtRet_t cnret;
  d_ptr_->dst_yuv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * 2));
  cnret = cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->dst_yuv_ptrs_mlu_), 2 * sizeof(char*));
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }

  d_ptr_->width = PAD_UP(width, ALIGN_R2Y_CVT);
  d_ptr_->height = height;
  d_ptr_->src_pix_fmt = getCNCVPixFmtFromPixindex(src_pix_fmt);
  d_ptr_->dst_pix_fmt = getCNCVPixFmtFromPixindex(dst_pix_fmt);
  d_ptr_->src_stride[0] = d_ptr_->width * getSizeOfDepth(getCNCVDepthFromIndex(depth)) *
                                 getPixFmtChannelNum(getCNCVPixFmtFromPixindex(src_pix_fmt));
  d_ptr_->dst_stride[0] = d_ptr_->width * getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->dst_stride[1] = d_ptr_->width * getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->depth = getCNCVDepthFromIndex(depth);

  *h = static_cast<void *>(d_ptr_);
  return 0;
}

int mluop_convert_rgbx2yuv_exec(HANDLE h,
                                void *input_rgbx,
                                void *output_y, void *output_uv) {
  CVRGBX2YUVPrivate *d_ptr_ = static_cast<CVRGBX2YUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     std::cout << "cnrt queue is nlll." << std::endl;
     return -1;
  }

  d_ptr_->dst_yuv_ptrs_cache_.push_back(std::make_pair(output_y, output_uv));
  d_ptr_->dst_yuv_ptrs_cpu_[0] = d_ptr_->dst_yuv_ptrs_cache_.front().first;
  d_ptr_->dst_yuv_ptrs_cpu_[1] = d_ptr_->dst_yuv_ptrs_cache_.front().second;
  d_ptr_->dst_yuv_ptrs_cache_.pop_front();

  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->dst_yuv_ptrs_mlu_, reinterpret_cast<void **>(d_ptr_->dst_yuv_ptrs_cpu_),
                     sizeof(char*) * 2, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return -1;
  }
  #if PRINT_TIME
  float time_use = 0;
  struct timeval end;
  struct timeval start;
  gettimeofday(&start, NULL);
  #endif

  const cncvImageDescriptor src_desc = {d_ptr_->width,
                                        d_ptr_->height,
                                        {d_ptr_->src_stride[0]},
                                        d_ptr_->src_pix_fmt,
                                        d_ptr_->src_color_space,
                                        d_ptr_->depth};

  const cncvImageDescriptor dst_desc = {d_ptr_->width,
                                        d_ptr_->height,
                                        {d_ptr_->dst_stride[0], d_ptr_->dst_stride[1]},
                                        d_ptr_->dst_pix_fmt,
                                        d_ptr_->dst_color_space,
                                        d_ptr_->depth};
  const struct cncvRect src_rois = {0, 0, d_ptr_->width, d_ptr_->height};

  cncvRgbxToYuv(d_ptr_->handle,
                src_desc,
                src_rois,
                reinterpret_cast<const void *>(input_rgbx),
                dst_desc,
                reinterpret_cast<void **>(d_ptr_->dst_yuv_ptrs_mlu_));
  #if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[cncv-kernel] time: %.3f ms\n", time_use/1000);
  #endif
  return 0;
}

int mluop_convert_rgbx2yuv_destroy(HANDLE h) {
  CVRGBX2YUVPrivate *d_ptr_ = static_cast<CVRGBX2YUVPrivate *>(h);
  if (d_ptr_->dst_yuv_ptrs_cpu_) {
      free(d_ptr_->dst_yuv_ptrs_cpu_);
      d_ptr_->dst_yuv_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_yuv_ptrs_mlu_) {
      cnrtFree(d_ptr_->dst_yuv_ptrs_mlu_);
      d_ptr_->dst_yuv_ptrs_mlu_ = nullptr;
  }
  d_ptr_->dst_yuv_ptrs_cache_.clear();

  if (d_ptr_->queue_) {
      auto ret = cnrtDestroyQueue(d_ptr_->queue_);
      if (ret != CNRT_RET_SUCCESS) {
      std::cout << "Destroy queue failed. Error code: %u" << std::endl;
      return -1;
      }
      d_ptr_->queue_ = nullptr;
  }
  if (d_ptr_->handle) {
      auto ret = cncvDestroy(d_ptr_->handle);
      if (ret != CNCV_STATUS_SUCCESS) {
      std::cout << "Destroy cncv handle failed. Error code: %u" << std::endl;
      return -1;
      }
      d_ptr_->handle = nullptr;
  }
  delete d_ptr_;
  d_ptr_ = nullptr;

  return 0;
}
