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

extern cncvStatus_t cncvYuv420spToRgbx(cncvHandle_t handle,
                                       const uint32_t batch_size,
                                       const cncvImageDescriptor src_desc,
                                       void **src,
                                       const cncvImageDescriptor dst_desc,
                                       void **dst,
                                       const size_t workspace_size,
                                       void *workspace);
extern cncvStatus_t cncvGetYuv420spToRgbxWorkspaceSize(cncvPixelFormat src_pixfmt,
                                                       cncvPixelFormat dst_pixfmt,
                                                       size_t *size);
static uint32_t getSizeOfDepth(cncvDepth_t depth) {
  if(depth == CNCV_DEPTH_8U) {
    return 1;
  } else if(depth == CNCV_DEPTH_16F) {
    return 2;
  } else if(depth == CNCV_DEPTH_32F) {
    return 4;
  }
  return 1;
}

static uint32_t getPixFmtChannelNum(cncvPixelFormat pixfmt) {
  if(pixfmt == CNCV_PIX_FMT_BGR || pixfmt == CNCV_PIX_FMT_RGB) {
    return 3;
  } else if(pixfmt == CNCV_PIX_FMT_ABGR || pixfmt == CNCV_PIX_FMT_ARGB ||
            pixfmt == CNCV_PIX_FMT_BGRA || pixfmt == CNCV_PIX_FMT_RGBA) {
    return 4;
  } else if(pixfmt == CNCV_PIX_FMT_NV12 || pixfmt == CNCV_PIX_FMT_NV21) {
    return 1;
  } else {
    printf("Don't support pixfmt(%d)\n", pixfmt);
    return 0;
  }
}

static cncvDepth_t getCNCVDepthFromIndex(const char* depth) {
  if(strcmp(depth, "8U") == 0 || strcmp(depth, "8u") == 0) {
    return CNCV_DEPTH_8U;
  } else if(strcmp(depth, "16F") == 0 || strcmp(depth, "16f") == 0) {
    return CNCV_DEPTH_16F;
  } else if(strcmp(depth, "32F") == 0 || strcmp(depth, "32f") == 0) {
    return CNCV_DEPTH_32F;
  } else {
    printf("Unsupported depth(%s)\n", depth);
    return CNCV_DEPTH_INVALID;
  }
}

static cncvPixelFormat getCNCVPixFmtFromPixindex(const char* pix_fmt) {
  if (strcmp(pix_fmt, "NV12") == 0 || strcmp(pix_fmt, "nv12") == 0) {
    return CNCV_PIX_FMT_NV12;
  } else if(strcmp(pix_fmt,"NV21") == 0 || strcmp(pix_fmt, "nv21") == 0) {
    return  CNCV_PIX_FMT_NV21;
  } else if(strcmp(pix_fmt, "RGB24") == 0 || strcmp(pix_fmt, "rgb24") == 0) {
    return CNCV_PIX_FMT_RGB;
  } else if(strcmp(pix_fmt, "BGR24") == 0 || strcmp(pix_fmt, "bgr24") == 0) {
    return CNCV_PIX_FMT_BGR;
  } else if(strcmp(pix_fmt, "ARGB") == 0 || strcmp(pix_fmt, "argb") == 0) {
    return CNCV_PIX_FMT_ARGB;
  } else if(strcmp(pix_fmt, "ABGR") == 0 || strcmp(pix_fmt, "abgr") == 0) {
    return CNCV_PIX_FMT_ABGR;
  } else if(strcmp(pix_fmt, "RGBA") == 0 || strcmp(pix_fmt, "rgba") == 0) {
    return  CNCV_PIX_FMT_RGBA;
  } else if (strcmp(pix_fmt, "BGRA") == 0 || strcmp(pix_fmt, "bgra") == 0) {
    return CNCV_PIX_FMT_BGRA;
  } else {
    printf("Unsupported pixfmt(%s)\n", pix_fmt);
    return  CNCV_PIX_FMT_INVALID;
  }
}

struct CVConvertYUV2RGBXPrivate {
 public:
  int batch_size = 1;
  int width, height;
  cnrtQueue_t queue_ = nullptr;
  cncvHandle_t handle;
  cncvImageDescriptor src_desc;
  cncvImageDescriptor dst_desc;
  cncvColorSpace srcColorSpace = CNCV_COLOR_SPACE_BT_601;

  size_t workspace_size = 0;
  void *workspace = nullptr;

  std::deque<std::pair<void*, void*>> src_yuv_ptrs_cache_;
  void **src_yuv_ptrs_cpu_;
  void **src_yuv_ptrs_mlu_;
  std::deque<void*> dst_rgbx_ptrs_cache_;
  void **dst_rgbx_ptrs_cpu_;
  void **dst_rgbx_ptrs_mlu_;

};  // CVResziePrivate

// according to handle(typedef void* handle) to deliver struct message
int mluop_convert_yuv2rgbx_init(HANDLE *h, int width, int height, const char *src_pix_fmt,
                                const char *dst_pix_fmt, const char *depth) {
  CVConvertYUV2RGBXPrivate *d_ptr_ = new CVConvertYUV2RGBXPrivate;
  cnrtCreateQueue(&d_ptr_->queue_);
  cncvCreate(&d_ptr_->handle);
  cncvSetQueue(d_ptr_->handle, d_ptr_->queue_);

  d_ptr_->width = PAD_UP(width, ALIGN_Y2R_CVT);
  d_ptr_->src_yuv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size * 2));
  d_ptr_->dst_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  cnrtRet_t cnret;
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_yuv_ptrs_mlu_), d_ptr_->batch_size * 2 * sizeof(char*));
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc mlu buffer failed. Error code:%d\n", cnret);
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_rgbx_ptrs_mlu_), d_ptr_->batch_size * sizeof(void*));
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc mlu buffer failed. Error code:%d\n", cnret);
    return -1;
  }

  cncvGetYuv420spToRgbxWorkspaceSize(getCNCVPixFmtFromPixindex(src_pix_fmt),
                                     getCNCVPixFmtFromPixindex(dst_pix_fmt),
                                     &d_ptr_->workspace_size);
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->workspace), d_ptr_->workspace_size);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc mlu buffer workspace failed. Error code:%d\n", cnret);
    return -1;
  }

  memset(&d_ptr_->src_desc, 0, sizeof(d_ptr_->src_desc));
  memset(&d_ptr_->dst_desc, 0, sizeof(d_ptr_->dst_desc));

  d_ptr_->src_desc.width = d_ptr_->width;
  d_ptr_->src_desc.height = height;
  d_ptr_->src_desc.pixel_fmt = getCNCVPixFmtFromPixindex(src_pix_fmt);
  d_ptr_->src_desc.stride[0] = d_ptr_->width * getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->src_desc.stride[1] = d_ptr_->width * getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->src_desc.color_space = d_ptr_->srcColorSpace;

  d_ptr_->dst_desc.width = d_ptr_->width;
  d_ptr_->dst_desc.height = height;
  d_ptr_->dst_desc.pixel_fmt = getCNCVPixFmtFromPixindex(dst_pix_fmt);
  d_ptr_->dst_desc.stride[0] = d_ptr_->width * getSizeOfDepth(getCNCVDepthFromIndex(depth)) *
                                getPixFmtChannelNum(getCNCVPixFmtFromPixindex(dst_pix_fmt));

  *h = static_cast<void *>(d_ptr_);
  return 0;
}

int mluop_convert_yuv2rgbx_exec(HANDLE h,
                                void *input_y, void *input_uv, void *output) {
  CVConvertYUV2RGBXPrivate *d_ptr_ = static_cast<CVConvertYUV2RGBXPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
      printf("Not create cnrt queue\n");
      return -1;
  }
  d_ptr_->src_yuv_ptrs_cache_.push_back(std::make_pair(input_y, input_uv));
  d_ptr_->dst_rgbx_ptrs_cache_.push_back(output);
  for (int bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_yuv_ptrs_cpu_[bi * 2] = d_ptr_->src_yuv_ptrs_cache_.front().first;
    d_ptr_->src_yuv_ptrs_cpu_[bi * 2 + 1] = d_ptr_->src_yuv_ptrs_cache_.front().second;
    d_ptr_->dst_rgbx_ptrs_cpu_[bi] = d_ptr_->dst_rgbx_ptrs_cache_.front();
    d_ptr_->src_yuv_ptrs_cache_.pop_front();
    d_ptr_->dst_rgbx_ptrs_cache_.pop_front();
  }

  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->src_yuv_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_yuv_ptrs_cpu_),
                     sizeof(char*) * d_ptr_->batch_size * 2, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
                     sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }
  #if PRINT_TIME
  float time_use = 0;
  struct timeval end;
  struct timeval start;
  gettimeofday(&start, NULL);
  #endif
  cncvStatus_t cncv_ret;
  cncv_ret = cncvYuv420spToRgbx(d_ptr_->handle,
                     d_ptr_->batch_size,
                     d_ptr_->src_desc,
                     reinterpret_cast<void**>(d_ptr_->src_yuv_ptrs_mlu_),
                     d_ptr_->dst_desc,
                     reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                     d_ptr_->workspace_size,
                     d_ptr_->workspace);
  if(cncv_ret != CNCV_STATUS_SUCCESS) {
    printf("Exec cncvYuv420spToRgbx failed, error code:%d\n", cncv_ret);
    return -1;
  }
  cncv_ret = cncvSyncQueue(d_ptr_->handle);
  if(cncv_ret != CNCV_STATUS_SUCCESS) {
    printf("Exec cncvSyncQueue failed,  error code:%d\n", cncv_ret);
    return -1;
  }
  #if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[cncv-kernel] time: %.3f ms\n", time_use/1000);
  #endif
  return 0;
}

int mluop_convert_yuv2rgbx_destroy(HANDLE h) {
  CVConvertYUV2RGBXPrivate *d_ptr_ = static_cast<CVConvertYUV2RGBXPrivate *>(h);
  if (d_ptr_->src_yuv_ptrs_cpu_) {
    free(d_ptr_->src_yuv_ptrs_cpu_);
    d_ptr_->src_yuv_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->src_yuv_ptrs_mlu_) {
    cnrtFree(d_ptr_->src_yuv_ptrs_mlu_);
    d_ptr_->src_yuv_ptrs_mlu_ = nullptr;
  }
  d_ptr_->src_yuv_ptrs_cache_.clear();

  if (d_ptr_->dst_rgbx_ptrs_cpu_) {
    free(d_ptr_->dst_rgbx_ptrs_cpu_);
    d_ptr_->dst_rgbx_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_rgbx_ptrs_mlu_) {
    cnrtFree(d_ptr_->dst_rgbx_ptrs_mlu_);
    d_ptr_->dst_rgbx_ptrs_mlu_ = nullptr;
  }
  d_ptr_->dst_rgbx_ptrs_cache_.clear();

  if (d_ptr_->workspace) {
    cnrtFree(d_ptr_->workspace);
    d_ptr_->workspace = nullptr;
  }

  if (d_ptr_->queue_) {
    auto ret = cnrtDestroyQueue(d_ptr_->queue_);
    if (ret != CNRT_RET_SUCCESS) {
    printf("Destroy queue failed. Error code: %d\n", ret);
    return -1;
    }
    d_ptr_->queue_ = nullptr;
  }
  if (d_ptr_->handle) {
    auto ret = cncvDestroy(d_ptr_->handle);
    if (ret != CNCV_STATUS_SUCCESS) {
    printf("Destroy cncv handle failed. Error code: %d\n", ret);
    return -1;
    }
    d_ptr_->handle = nullptr;
  }
  delete d_ptr_;
  d_ptr_ = nullptr;

  return 0;
}
