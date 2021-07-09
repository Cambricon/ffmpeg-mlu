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

#define CNRT_ERROR_CHECK(ret)                                                  \
  if (ret != CNRT_RET_SUCCESS) {                                               \
    fprintf(stderr, "error occur, func: %s, line: %d\n", __func__, __LINE__);  \
    return 0;                                                                 \
  }

extern cncvStatus_t cncvResizeRgbx(cncvHandle_t handle,
                                   uint32_t batch_size,
                                   const cncvImageDescriptor src_desc,
                                   const cncvRect *src_rois,
                                   void **src,
                                   const cncvImageDescriptor dst_desc,
                                   const cncvRect *dst_rois,
                                   void **dst,
                                   const size_t workspace_size,
                                   void *workspace,
                                   cncvInterpolation interpolation);
extern cncvStatus_t cncvGetResizeRgbxWorkspaceSize(const uint32_t batch_size,
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

struct CVResizeRgbxPrivate {
 public:
  uint32_t input_w, input_h;
  uint32_t input_stride_in_bytes;
  uint32_t output_w, output_h;
  uint32_t output_stride_in_bytes;

  uint32_t batch_size = 1;
  uint32_t depth_size;
  uint32_t pix_chn_num;
  size_t workspace_size = 0;
  void *workspace = nullptr;

  cncvHandle_t handle;
  cnrtQueue_t queue_ = nullptr;
  cncvImageDescriptor src_desc;
  cncvImageDescriptor dst_desc;
  cncvRect src_rois;
  cncvRect dst_rois;

  std::deque<void*> src_rgbx_ptrs_cache_;
  void **src_rgbx_ptrs_cpu_ = nullptr;
  void **src_rgbx_ptrs_mlu_ = nullptr;
  std::deque<void*> dst_rgbx_ptrs_cache_;
  void **dst_rgbx_ptrs_cpu_ = nullptr;
  void **dst_rgbx_ptrs_mlu_ = nullptr;
};

int mluop_resize_rgbx_init(HANDLE *h,
                           int input_w,
                           int input_h,
                           int output_w,
                           int output_h,
                           const char *pix_fmt,
                           const char *depth) {
  CVResizeRgbxPrivate *d_ptr_ = new CVResizeRgbxPrivate;

  cnrtRet_t cnret;
  cnrtCreateQueue(&d_ptr_->queue_);
  cncvCreate(&d_ptr_->handle);
  cncvSetQueue(d_ptr_->handle, d_ptr_->queue_);

  d_ptr_->input_h = input_h;
  d_ptr_->input_w = PAD_UP(input_w, ALIGN_R_SCALE);
  d_ptr_->output_h = output_h;
  d_ptr_->output_w = PAD_UP(output_w, ALIGN_R_SCALE);

  d_ptr_->depth_size = getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->pix_chn_num = getPixFmtChannelNum(getCNCVPixFmtFromPixindex(pix_fmt));
  d_ptr_->input_stride_in_bytes = d_ptr_->input_w * d_ptr_->pix_chn_num * d_ptr_->depth_size;
  d_ptr_->output_stride_in_bytes = d_ptr_->output_w * d_ptr_->pix_chn_num * d_ptr_->depth_size;

  d_ptr_->src_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  d_ptr_->dst_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));

  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_rgbx_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_rgbx_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }

  cncvGetResizeRgbxWorkspaceSize(d_ptr_->batch_size, &d_ptr_->workspace_size);
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
  d_ptr_->src_desc.stride[0] = d_ptr_->input_stride_in_bytes;
  d_ptr_->src_desc.depth = getCNCVDepthFromIndex(depth);

  d_ptr_->dst_desc.width = d_ptr_->output_w;
  d_ptr_->dst_desc.height = d_ptr_->output_h;
  d_ptr_->dst_desc.pixel_fmt = getCNCVPixFmtFromPixindex(pix_fmt);
  d_ptr_->dst_desc.stride[0] = d_ptr_->output_stride_in_bytes;
  d_ptr_->dst_desc.depth = getCNCVDepthFromIndex(depth);

  *h = static_cast<void *>(d_ptr_);
  return 0;
}

int mluop_resize_rgbx_exec(HANDLE h,
                           void *input, void *output) {
  CVResizeRgbxPrivate *d_ptr_ = static_cast<CVResizeRgbxPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     std::cout << "cnrt queue is nlll." << std::endl;
     return -1;
  }
  d_ptr_->src_rgbx_ptrs_cache_.push_back(input); // input is mlu src ptr, ..._ptrs_cache_ is cpu ptr for batch address
  d_ptr_->dst_rgbx_ptrs_cache_.push_back(output); //
  for (uint32_t bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_rgbx_ptrs_cpu_[bi] = d_ptr_->src_rgbx_ptrs_cache_.front();
    d_ptr_->dst_rgbx_ptrs_cpu_[bi] = d_ptr_->dst_rgbx_ptrs_cache_.front();
    d_ptr_->src_rgbx_ptrs_cache_.pop_front();
    d_ptr_->dst_rgbx_ptrs_cache_.pop_front();
  }

  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->src_rgbx_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
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
  cncvResizeRgbx(d_ptr_->handle,
                 d_ptr_->batch_size,
                 d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
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

int mluop_resize_rgbx_destroy(HANDLE h) {
  CVResizeRgbxPrivate *d_ptr_ = static_cast<CVResizeRgbxPrivate *>(h);
  if (d_ptr_) {
    if (d_ptr_->src_rgbx_ptrs_cpu_) {
      free(d_ptr_->src_rgbx_ptrs_cpu_);
      d_ptr_->src_rgbx_ptrs_cpu_ = nullptr;
    }
    if (d_ptr_->src_rgbx_ptrs_mlu_) {
      cnrtFree(d_ptr_->src_rgbx_ptrs_mlu_);
      d_ptr_->src_rgbx_ptrs_mlu_ = nullptr;
    }
    d_ptr_->src_rgbx_ptrs_cache_.clear();

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
  }
  return 0;
}
