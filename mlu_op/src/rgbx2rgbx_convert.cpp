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
#include <sys/time.h>

#include <chrono>
#include <ctime>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "cnrt.h"
#include "mluop.h"

using std::string;
using std::to_string;

extern cncvStatus_t cncvResizeRgbx(cncvHandle_t handle, uint32_t batch_size,
                                   const cncvImageDescriptor src_desc,
                                   const cncvRect *src_rois, void **src,
                                   const cncvImageDescriptor dst_desc,
                                   const cncvRect *dst_rois, void **dst,
                                   const size_t workspace_size, void *workspace,
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
    printf("Unsupported pixfmt(%d)\n", pixfmt);
    return 0;
  }
}

static cncvDepth_t getCNCVDepthFromIndex(const char *depth) {
  if (strncmp(depth, "8U", 2) == 0 || strncmp(depth, "8u", 2) == 0) {
    return CNCV_DEPTH_8U;
  } else if (strncmp(depth, "16F", 3) == 0 || strncmp(depth, "16f", 3) == 0) {
    return CNCV_DEPTH_16F;
  } else if (strncmp(depth, "32F", 3) == 0 || strncmp(depth, "32f", 3) == 0) {
    return CNCV_DEPTH_32F;
  } else {
    printf("Unsupported depth(%s)\n", depth);
    return CNCV_DEPTH_INVALID;
  }
}

static cncvPixelFormat getCNCVPixFmtFromPixindex(const char *pix_fmt) {
  if (strncmp(pix_fmt, "NV12", 4) == 0 || strncmp(pix_fmt, "nv12", 4) == 0) {
    return CNCV_PIX_FMT_NV12;
  } else if (strncmp(pix_fmt, "NV21", 4) == 0 ||
             strncmp(pix_fmt, "nv21", 4) == 0) {
    return CNCV_PIX_FMT_NV21;
  } else if (strncmp(pix_fmt, "RGB24", 5) == 0 ||
             strncmp(pix_fmt, "rgb24", 5) == 0) {
    return CNCV_PIX_FMT_RGB;
  } else if (strncmp(pix_fmt, "BGR24", 5) == 0 ||
             strncmp(pix_fmt, "bgr24", 5) == 0) {
    return CNCV_PIX_FMT_BGR;
  } else if (strncmp(pix_fmt, "ARGB", 4) == 0 ||
             strncmp(pix_fmt, "argb", 4) == 0) {
    return CNCV_PIX_FMT_ARGB;
  } else if (strncmp(pix_fmt, "ABGR", 4) == 0 ||
             strncmp(pix_fmt, "abgr", 4) == 0) {
    return CNCV_PIX_FMT_ABGR;
  } else if (strncmp(pix_fmt, "RGBA", 4) == 0 ||
             strncmp(pix_fmt, "rgba", 4) == 0) {
    return CNCV_PIX_FMT_RGBA;
  } else if (strncmp(pix_fmt, "BGRA", 4) == 0 ||
             strncmp(pix_fmt, "bgra", 4) == 0) {
    return CNCV_PIX_FMT_BGRA;
  } else {
    printf("Unsupported pixfmt(%s)\n", pix_fmt);
    return CNCV_PIX_FMT_INVALID;
  }
}

struct CvRgbx2RgbxPrivate {
 public:
  uint32_t width, height;
  uint32_t depth;
  cncvColorSpace srcColorSpace = CNCV_COLOR_SPACE_BT_601;

  cncvHandle_t handle;
  cnrtQueue_t queue = nullptr;
  cncvImageDescriptor src_desc;
  cncvImageDescriptor dst_desc;
  cncvRect src_rois;
  cncvRect dst_rois;

  float sw_time = 0.0;
  float hw_time = 0.0;
  struct timeval end;
  struct timeval start;
  cnrtNotifier_t event_begin = nullptr;
  cnrtNotifier_t event_end   = nullptr;

  void **src_ptrs_cpu;
  void **src_ptrs_mlu;
  void **dst_ptrs_cpu;
  void **dst_ptrs_mlu;
};

/* Realize of rgbx2rgbx initial function
 * Set the width, height and roi space data
 * to correct value.
*/
int MluopConvertRgbx2RgbxInit(HANDLE *h, int width, int height,
                              const char *src_pix_fmt, const char *dst_pix_fmt,
                              const char *depth) {
  CvRgbx2RgbxPrivate *d_ptr_ = new CvRgbx2RgbxPrivate;
  cnrtCreateQueue(&d_ptr_->queue);
  cncvCreate(&d_ptr_->handle);
  cncvSetQueue(d_ptr_->handle, d_ptr_->queue);

  d_ptr_->width = PAD_UP(width, ALIGN_Y2R_CVT);
  d_ptr_->src_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));
  d_ptr_->dst_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));

  cnrtRet_t cnret;
  cnret = cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_ptrs_mlu),
                     sizeof(char *));
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc mlu buffer failed. Error code:%d\n", cnret);
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->dst_ptrs_mlu),
                     sizeof(void *));
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc mlu buffer failed. Error code:%d\n", cnret);
    return -1;
  }

  memset(&d_ptr_->src_desc, 0, sizeof(d_ptr_->src_desc));
  memset(&d_ptr_->dst_desc, 0, sizeof(d_ptr_->dst_desc));
  d_ptr_->src_desc.width = d_ptr_->width;
  d_ptr_->src_desc.height = height;
  d_ptr_->src_desc.pixel_fmt = getCNCVPixFmtFromPixindex(src_pix_fmt);
  d_ptr_->src_desc.stride[0] =
      d_ptr_->width * getSizeOfDepth(getCNCVDepthFromIndex(depth)) *
      getPixFmtChannelNum(getCNCVPixFmtFromPixindex(src_pix_fmt));
  d_ptr_->src_desc.color_space = d_ptr_->srcColorSpace;
  d_ptr_->dst_desc.width = d_ptr_->width;
  d_ptr_->dst_desc.height = height;
  d_ptr_->dst_desc.pixel_fmt = getCNCVPixFmtFromPixindex(dst_pix_fmt);
  d_ptr_->dst_desc.stride[0] =
      d_ptr_->width * getSizeOfDepth(getCNCVDepthFromIndex(depth)) *
      getPixFmtChannelNum(getCNCVPixFmtFromPixindex(dst_pix_fmt));
  d_ptr_->dst_desc.color_space = d_ptr_->srcColorSpace;

  memset(&d_ptr_->src_rois, 0, sizeof(d_ptr_->src_rois));
  memset(&d_ptr_->dst_rois, 0, sizeof(d_ptr_->dst_rois));
  d_ptr_->src_rois.x = 0;
  d_ptr_->src_rois.y = 0;
  d_ptr_->src_rois.w = width;
  d_ptr_->src_rois.h = height;
  d_ptr_->dst_rois.x = 0;
  d_ptr_->dst_rois.y = 0;
  d_ptr_->dst_rois.w = width;
  d_ptr_->dst_rois.h = height;

  #ifdef DEBUG
  if (CNRT_RET_SUCCESS != cnrtCreateNotifier(&d_ptr_->event_begin)) {
    printf("cnrtCreateNotifier eventBegin failed\n");
  }
  if (CNRT_RET_SUCCESS != cnrtCreateNotifier(&d_ptr_->event_end)) {
    printf("cnrtCreateNotifier eventEnd failed\n");
  }
  #endif

  *h = static_cast<void *>(d_ptr_);

  return 0;
}

/* Realize of rgbx2rgbx exec function
 * The cncv func: cncvRgbxToRgbx is called to perform the
 * transformation of rgbx to rgbx.
 * Before transform, use cnrtMemcpy to copy data to mlu
 * device.
 * At last, call Sync func to Make sure the calculations
 * are complete.
*/
int MluopConvertRgbx2RgbxExec(HANDLE h, void *input_rgbx, void *output_rgbx) {
  CvRgbx2RgbxPrivate *d_ptr_ = static_cast<CvRgbx2RgbxPrivate *>(h);
  if (nullptr == d_ptr_->queue) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  d_ptr_->src_ptrs_cpu[0] = input_rgbx;
  d_ptr_->dst_ptrs_cpu[0] = output_rgbx;

  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->src_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->dst_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }

  cncvStatus_t cncv_ret;
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue);
  #endif
  cncv_ret = cncvRgbxToRgbx(d_ptr_->handle,
                            1, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<void **>(d_ptr_->src_ptrs_mlu),
                            d_ptr_->dst_desc,
                            d_ptr_->dst_rois,
                            reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu));
  if (cncv_ret != CNCV_STATUS_SUCCESS) {
    printf("Exec cncvYuv420spToRgbx failed, error code:%d\n", cncv_ret);
    return -1;
  }
  #ifdef DEBUG
  cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue);
  #endif
  cncv_ret = cncvSyncQueue(d_ptr_->handle);
  if (cncv_ret != CNCV_STATUS_SUCCESS) {
    printf("Exec cncvSyncQueue failed,  error code:%d\n", cncv_ret);
    return -1;
  }
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  cnrtNotifierDuration(d_ptr_->event_begin, d_ptr_->event_end, &d_ptr_->hw_time);
  d_ptr_->sw_time = (d_ptr_->end.tv_sec - d_ptr_->start.tv_sec) * 1000000
                    + (d_ptr_->end.tv_usec - d_ptr_->start.tv_usec);
  printf("hw time: %.3f ms, sw time: %.3f ms\n",
        d_ptr_->hw_time/1000.f, d_ptr_->sw_time/1000.f);
  #endif

  return 0;
}

/* Realize of rgbx2rgbx destroy function.
 * Use free() to release cpu space.use cnrtFree() to release mlu
 * space and cncvDestroy() to destroy cncnQueue.
*/
int MluopConvertRgbx2RgbxDestroy(HANDLE h) {
  CvRgbx2RgbxPrivate *d_ptr_ = static_cast<CvRgbx2RgbxPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop cvt rgbx2rgbx op not init\n");
    return 0;
  }
  #ifdef DEBUG
  if (d_ptr_->event_begin) cnrtDestroyNotifier(&d_ptr_->event_begin);
  if (d_ptr_->event_end)   cnrtDestroyNotifier(&d_ptr_->event_end);
  #endif
  if (d_ptr_->src_ptrs_cpu) {
    free(d_ptr_->src_ptrs_cpu);
    d_ptr_->src_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_ptrs_mlu) {
    cnrtFree(d_ptr_->src_ptrs_mlu);
    d_ptr_->src_ptrs_mlu = nullptr;
  }
  if (d_ptr_->dst_ptrs_cpu) {
    free(d_ptr_->dst_ptrs_cpu);
    d_ptr_->dst_ptrs_cpu = nullptr;
  }
  if (d_ptr_->dst_ptrs_mlu) {
    cnrtFree(d_ptr_->dst_ptrs_mlu);
    d_ptr_->dst_ptrs_mlu = nullptr;
  }
  if (d_ptr_->queue) {
    auto ret = cnrtDestroyQueue(d_ptr_->queue);
    if (ret != CNRT_RET_SUCCESS) {
      printf("Destroy queue failed. Error code: %d\n", ret);
      return -1;
    }
    d_ptr_->queue = nullptr;
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