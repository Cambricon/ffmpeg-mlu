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

extern cncvStatus_t cncvResizeYuv420sp(cncvHandle_t handle,
                                       const uint32_t batch_size,
                                       const cncvImageDescriptor src_desc,
                                       const cncvRect  *src_rois,
                                       void **src_y,
                                       void **src_uv,
                                       const cncvImageDescriptor dst_desc,
                                       void **dst_y,
                                       void **dst_uv,
                                       const cncvRect *dst_rois,
                                       const size_t workspace_size,
                                       void *workspace,
                                       cncvInterpolation interpolation);
extern cncvStatus_t cncvGetResizeYuv420spWorkspaceSize(const uint32_t batch_size,
                                                       size_t *workspace_size);
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
  } else if (strcmp(pix_fmt , "RGB") == 0 ||
             strcmp(pix_fmt , "rgb") == 0) {
        return  CNCV_PIX_FMT_RGB;
  } else if (strcmp(pix_fmt , "BGR") == 0 ||
             strcmp(pix_fmt , "bgr") == 0) {
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

struct CVResizeYUVPrivate {
 public:
  int batch_size = 1;
  int input_w, input_h, output_w, output_h;
  cnrtDim3_t dim_ = {4, 1, 1};
  cnrtFunctionType_t ftype_ = CNRT_FUNC_TYPE_UNION1;
  cnrtQueue_t queue_ = nullptr;
  cncvHandle_t handle;
  cncvImageDescriptor src_desc;
  cncvImageDescriptor dst_desc;
  cncvRect src_rois;
  cncvRect dst_rois;

  size_t workspace_size = 0;
  void *workspace = nullptr;

  std::deque<std::pair<void*, void*>> src_yuv_ptrs_cache_;
  void **src_y_ptrs_cpu_ = nullptr, **src_uv_ptrs_cpu_ = nullptr;
  void **src_y_ptrs_mlu_ = nullptr, **src_uv_ptrs_mlu_ = nullptr;
  std::deque<std::pair<void*, void*>> dst_yuv_ptrs_cache_;
  void **dst_y_ptrs_cpu_ = nullptr, **dst_uv_ptrs_cpu_ = nullptr;
  void **dst_y_ptrs_mlu_ = nullptr, **dst_uv_ptrs_mlu_ = nullptr;

  std::string estr_;
};  // CVResziePrivate

// according to handle(typedef void* handle) to deliver struct message
int mluop_resize_yuv_init(HANDLE *h,
                          int input_w, int input_h,
                          int output_w, int output_h,
                          const char *depth, const char *pix_fmt) {
  CVResizeYUVPrivate *d_ptr_ = new CVResizeYUVPrivate;
  cnrtCreateQueue(&d_ptr_->queue_);
  cncvCreate(&d_ptr_->handle);
  cncvSetQueue(d_ptr_->handle, d_ptr_->queue_);

  d_ptr_->input_w = PAD_UP(input_w, ALIGN_Y_SCALE);
  d_ptr_->output_w = PAD_UP(output_w, ALIGN_Y_SCALE);
  d_ptr_->input_h = input_h;
  d_ptr_->output_h = output_h;
  d_ptr_->src_y_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  d_ptr_->src_uv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  d_ptr_->dst_y_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  d_ptr_->dst_uv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  cnrtRet_t cnret;
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_y_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_uv_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_y_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_uv_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }

  cncvGetResizeYuv420spWorkspaceSize(d_ptr_->batch_size, &d_ptr_->workspace_size);
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->workspace), d_ptr_->workspace_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer workspace failed. Error code:" << std::endl;
    return -1;
  }

  d_ptr_->src_rois.h = d_ptr_->input_h;
  d_ptr_->src_rois.w = d_ptr_->input_w;
  d_ptr_->src_rois.x = 0;
  d_ptr_->src_rois.y = 0;

  d_ptr_->dst_rois.h = d_ptr_->output_h;
  d_ptr_->dst_rois.w = d_ptr_->output_w;
  d_ptr_->dst_rois.x = 0;
  d_ptr_->dst_rois.y = 0;

  d_ptr_->src_desc.width = d_ptr_->input_w;
  d_ptr_->src_desc.height = d_ptr_->input_h;
  d_ptr_->src_desc.pixel_fmt = getCNCVPixFmtFromPixindex(pix_fmt);
  d_ptr_->src_desc.stride[0] = d_ptr_->input_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->src_desc.stride[1] = d_ptr_->input_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->src_desc.depth = getCNCVDepthFromIndex(depth);

  d_ptr_->dst_desc.width = d_ptr_->output_w;
  d_ptr_->dst_desc.height = d_ptr_->output_h;
  d_ptr_->dst_desc.pixel_fmt = getCNCVPixFmtFromPixindex(pix_fmt);
  d_ptr_->dst_desc.stride[0] = d_ptr_->output_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->dst_desc.stride[1] = d_ptr_->output_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->dst_desc.depth = getCNCVDepthFromIndex(depth);

  *h = static_cast<void *>(d_ptr_);
  return 0;
}

int mluop_resize_yuv_exec(HANDLE h,
                          void *input_y, void *input_uv,
                          void *output_y, void *output_uv) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     std::cout << "cnrt queue is nlll." << std::endl;
     return -1;
  }
  d_ptr_->src_yuv_ptrs_cache_.push_back(std::make_pair(input_y, input_uv));
  d_ptr_->dst_yuv_ptrs_cache_.push_back(std::make_pair(output_y, output_uv));
  for (int bi = 0; bi < d_ptr_->batch_size; bi++) {
    d_ptr_->src_y_ptrs_cpu_[bi] = d_ptr_->src_yuv_ptrs_cache_.front().first;
    d_ptr_->src_uv_ptrs_cpu_[bi] = d_ptr_->src_yuv_ptrs_cache_.front().second;
    d_ptr_->dst_y_ptrs_cpu_[bi] = d_ptr_->dst_yuv_ptrs_cache_.front().first;
    d_ptr_->dst_uv_ptrs_cpu_[bi] = d_ptr_->dst_yuv_ptrs_cache_.front().second;
    d_ptr_->src_yuv_ptrs_cache_.pop_front();
    d_ptr_->dst_yuv_ptrs_cache_.pop_front();
  }
  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->src_y_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_y_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->src_uv_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_uv_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_y_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_y_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_uv_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_uv_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
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
  cncvResizeYuv420sp(d_ptr_->handle,
                     d_ptr_->batch_size,
                     d_ptr_->src_desc,
                     &d_ptr_->src_rois,
                     reinterpret_cast<void**>(d_ptr_->src_y_ptrs_mlu_),
                     reinterpret_cast<void**>(d_ptr_->src_uv_ptrs_mlu_),
                     d_ptr_->dst_desc,
                     reinterpret_cast<void**>(d_ptr_->dst_y_ptrs_mlu_),
                     reinterpret_cast<void**>(d_ptr_->dst_uv_ptrs_mlu_),
                     &d_ptr_->dst_rois,
                     d_ptr_->workspace_size,
                     d_ptr_->workspace,
                     CNCV_INTER_BILINEAR);
  #if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[cncv-kernel] time: %.3f ms\n", time_use/1000);
  #endif
  return 0;
}

int mluop_resize_yuv_destroy(HANDLE h) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (d_ptr_->src_y_ptrs_cpu_) {
      free(d_ptr_->src_y_ptrs_cpu_);
      d_ptr_->src_y_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->src_uv_ptrs_cpu_) {
      free(d_ptr_->src_uv_ptrs_cpu_);
      d_ptr_->src_uv_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->src_y_ptrs_mlu_) {
      cnrtFree(d_ptr_->src_y_ptrs_mlu_);
      d_ptr_->src_y_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->src_uv_ptrs_mlu_) {
      cnrtFree(d_ptr_->src_uv_ptrs_mlu_);
      d_ptr_->src_uv_ptrs_mlu_ = nullptr;
  }
  d_ptr_->src_yuv_ptrs_cache_.clear();

  if (d_ptr_->dst_y_ptrs_cpu_) {
      free(d_ptr_->dst_y_ptrs_cpu_);
      d_ptr_->dst_y_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_uv_ptrs_cpu_) {
      free(d_ptr_->dst_uv_ptrs_cpu_);
      d_ptr_->dst_uv_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_y_ptrs_mlu_) {
      cnrtFree(d_ptr_->dst_y_ptrs_mlu_);
      d_ptr_->dst_y_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->dst_uv_ptrs_mlu_) {
      cnrtFree(d_ptr_->dst_uv_ptrs_mlu_);
      d_ptr_->dst_uv_ptrs_mlu_ = nullptr;
  }
  d_ptr_->dst_yuv_ptrs_cache_.clear();

  if (d_ptr_->workspace) {
      cnrtFree(d_ptr_->workspace);
      d_ptr_->workspace = nullptr;
  }

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
