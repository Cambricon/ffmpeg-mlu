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

#define PRINT_TIME 0

extern cncvStatus_t cncvResizeConvert(
    cncvHandle_t handle, uint32_t batch_size,
    const cncvImageDescriptor *psrc_descs, const cncvRect *src_rois,
    void **src_y, void **src_uv, const cncvImageDescriptor pdst_descs,
    const cncvRect *dst_rois, void **dst, const size_t workspace_size,
    void *workspace, cncvInterpolation interpolation);
extern cncvStatus_t cncvGetResizeConvertWorkspaceSize(
    const uint32_t batch_size, const cncvImageDescriptor *psrc_descs,
    const cncvRect *src_rois, const cncvImageDescriptor *pdst_descs,
    const cncvRect *dst_rois, size_t *workspace_size);
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

struct CvResizeCvtPrivate {
 public:
  cncvDepth_t depth;
  uint32_t batch_size = 1;
  cncvColorSpace color_space = CNCV_COLOR_SPACE_BT_601;

  uint32_t src_width, src_height;
  uint32_t dst_width, dst_height;
  cncvImageDescriptor src_desc;
  cncvImageDescriptor dst_desc;
  cncvRect src_rois;
  cncvRect dst_rois;

  cncvHandle_t handle;
  cnrtQueue_t queue = nullptr;
  size_t workspace_size = 0;
  void *workspace = nullptr;

  void **src_y_ptrs_cpu;
  void **src_uv_ptrs_cpu;
  void **src_y_ptrs_mlu;
  void **src_uv_ptrs_mlu;
  void **dst_ptrs_cpu;
  void **dst_ptrs_mlu;
};

int MluopResizeCvtInit(HANDLE *h, int src_width, int src_height, int dst_width,
                       int dst_height, const char *src_pix_fmt,
                       const char *dst_pix_fmt, const char *depth) {
  CvResizeCvtPrivate *d_ptr_ = new CvResizeCvtPrivate;
  cnrtCreateQueue(&d_ptr_->queue);
  cncvCreate(&d_ptr_->handle);
  cncvSetQueue(d_ptr_->handle, d_ptr_->queue);

  d_ptr_->src_width = PAD_UP(src_width, ALIGN_RESIZE_CVT);
  d_ptr_->src_height = PAD_UP(src_height, ALIGN_RESIZE_CVT);
  d_ptr_->dst_width = PAD_UP(dst_width, ALIGN_RESIZE_CVT);
  d_ptr_->dst_height = PAD_UP(dst_height, ALIGN_RESIZE_CVT);

  memset(&d_ptr_->src_desc, 0, sizeof(d_ptr_->src_desc));
  d_ptr_->src_desc.width = d_ptr_->src_width;
  d_ptr_->src_desc.height = d_ptr_->src_height;
  d_ptr_->src_desc.pixel_fmt = getCNCVPixFmtFromPixindex(src_pix_fmt);
  d_ptr_->src_desc.stride[0] =
      d_ptr_->src_width * getSizeOfDepth(getCNCVDepthFromIndex(depth)) *
      getPixFmtChannelNum(getCNCVPixFmtFromPixindex(src_pix_fmt));
  d_ptr_->src_desc.stride[1] = d_ptr_->src_desc.stride[0];
  d_ptr_->src_desc.color_space = d_ptr_->color_space;

  memset(&d_ptr_->dst_desc, 0, sizeof(d_ptr_->dst_desc));
  d_ptr_->dst_desc.width = d_ptr_->dst_width;
  d_ptr_->dst_desc.height = d_ptr_->dst_height;
  d_ptr_->dst_desc.pixel_fmt = getCNCVPixFmtFromPixindex(dst_pix_fmt);
  d_ptr_->dst_desc.stride[0] =
      d_ptr_->dst_width * getSizeOfDepth(getCNCVDepthFromIndex(depth)) *
      getPixFmtChannelNum(getCNCVPixFmtFromPixindex(dst_pix_fmt));
  d_ptr_->dst_desc.color_space = d_ptr_->color_space;

  memset(&d_ptr_->src_rois, 0, sizeof(d_ptr_->src_rois));
  memset(&d_ptr_->dst_rois, 0, sizeof(d_ptr_->dst_rois));
  d_ptr_->src_rois.x = 0;
  d_ptr_->src_rois.y = 0;
  d_ptr_->src_rois.w = src_width;
  d_ptr_->src_rois.h = src_height;
  d_ptr_->dst_rois.x = 0;
  d_ptr_->dst_rois.y = 0;
  d_ptr_->dst_rois.w = dst_width;
  d_ptr_->dst_rois.h = dst_height;

  d_ptr_->src_y_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));
  d_ptr_->src_uv_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));
  d_ptr_->dst_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));

  cnrtRet_t cnret;
  cnret = cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_y_ptrs_mlu),
                     sizeof(char *));
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc y mlu buffer failed. Error code:%d\n", cnret);
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_uv_ptrs_mlu),
                     sizeof(char *));
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc uv mlu buffer failed. Error code:%d\n", cnret);
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->dst_ptrs_mlu),
                     sizeof(void *));
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc mlu buffer failed. Error code:%d\n", cnret);
    return -1;
  }

  cncvGetResizeConvertWorkspaceSize(d_ptr_->batch_size, &d_ptr_->src_desc,
                                    &d_ptr_->src_rois, &d_ptr_->dst_desc,
                                    &d_ptr_->dst_rois, &d_ptr_->workspace_size);
  cnret = cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->workspace),
                     d_ptr_->workspace_size);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc mlu buffer workspace failed. Error code:%d\n", cnret);
    return -1;
  }

  *h = static_cast<void *>(d_ptr_);
  return 0;
}

int MluopResizeCvtExec(HANDLE h, void *input_y, void *input_uv,
                       void *output_rgbx) {
  CvResizeCvtPrivate *d_ptr_ = static_cast<CvResizeCvtPrivate *>(h);
  if (nullptr == d_ptr_->queue) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  d_ptr_->src_y_ptrs_cpu[0] = input_y;
  d_ptr_->src_uv_ptrs_cpu[0] = input_uv;
  d_ptr_->dst_ptrs_cpu[0] = output_rgbx;

  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->src_y_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_y_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host y to device failed. Error code:%d\n", cnret);
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->src_uv_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host uv to device failed. Error code:%d\n", cnret);
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
  cncv_ret = cncvResizeConvert(
      d_ptr_->handle, 1, &d_ptr_->src_desc, &d_ptr_->src_rois,
      reinterpret_cast<void **>(d_ptr_->src_y_ptrs_mlu),
      reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_mlu), &d_ptr_->dst_desc,
      &d_ptr_->dst_rois, reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu),
      d_ptr_->workspace_size, d_ptr_->workspace, CNCV_INTER_BILINEAR);
  if (cncv_ret != CNCV_STATUS_SUCCESS) {
    printf("Exec cncvResizeCvtYuv420spToRgbx failed, error code:%d\n",
           cncv_ret);
    return -1;
  }
  cncv_ret = cncvSyncQueue(d_ptr_->handle);
  if (cncv_ret != CNCV_STATUS_SUCCESS) {
    printf("Exec cncvSyncQueue failed,  error code:%d\n", cncv_ret);
    return -1;
  }
  return 0;
}

int MluopResizeCvtPadExec(HANDLE h, void *input_y, void *input_uv,
                          void *output_rgbx) {
  CvResizeCvtPrivate *d_ptr_ = static_cast<CvResizeCvtPrivate *>(h);
  const float EPSINON = 0.00001f;

  if (nullptr == d_ptr_->queue) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  d_ptr_->src_y_ptrs_cpu[0] = input_y;
  d_ptr_->src_uv_ptrs_cpu[0] = input_uv;
  d_ptr_->dst_ptrs_cpu[0] = output_rgbx;

  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->src_y_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_y_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host y to device failed. Error code:%d\n", cnret);
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->src_uv_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host uv to device failed. Error code:%d\n", cnret);
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->dst_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }
  cnret = cnrtMemset(output_rgbx, 0,
                     d_ptr_->dst_desc.stride[0] * d_ptr_->dst_desc.height);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memset device value failed. Error code:%d\n", cnret);
    return -1;
  }

  int low_bound_p, low_bound_len;
  float src_scale = (float)d_ptr_->src_desc.width / d_ptr_->src_desc.height;
  float dst_scale = (float)d_ptr_->dst_desc.width / d_ptr_->dst_desc.height;
  float dis = fabs(src_scale - dst_scale);
  if (dis > EPSINON) {
    if (src_scale < dst_scale) {
      d_ptr_->dst_rois.y = 0;
      d_ptr_->dst_rois.h = d_ptr_->dst_desc.height;
      low_bound_len = (d_ptr_->dst_desc.height * d_ptr_->src_desc.width /
                       d_ptr_->src_desc.height) /
                      2;
      d_ptr_->dst_rois.w = low_bound_len * 2;
      low_bound_p = (d_ptr_->dst_desc.width - d_ptr_->dst_rois.w) / 4;
      d_ptr_->dst_rois.x = low_bound_p * 2;
    } else {
      d_ptr_->dst_rois.x = 0;
      d_ptr_->dst_rois.w = d_ptr_->dst_desc.width;
      low_bound_len = (d_ptr_->dst_desc.width * d_ptr_->src_desc.height /
                       d_ptr_->src_desc.width) /
                      2;
      d_ptr_->dst_rois.h = low_bound_len * 2;
      low_bound_p = (d_ptr_->dst_desc.height - d_ptr_->dst_rois.h) / 4;
      d_ptr_->dst_rois.y = low_bound_p * 2;
    }
  }

  cncvStatus_t cncv_ret;
  cncv_ret = cncvResizeConvert(
      d_ptr_->handle, 1, &d_ptr_->src_desc, &d_ptr_->src_rois,
      reinterpret_cast<void **>(d_ptr_->src_y_ptrs_mlu),
      reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_mlu), &d_ptr_->dst_desc,
      &d_ptr_->dst_rois, reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu),
      d_ptr_->workspace_size, d_ptr_->workspace, CNCV_INTER_BILINEAR);
  if (cncv_ret != CNCV_STATUS_SUCCESS) {
    printf("Exec cncvResizeCvtYuv420spToRgbx failed, error code:%d\n",
           cncv_ret);
    return -1;
  }
  cncv_ret = cncvSyncQueue(d_ptr_->handle);
  if (cncv_ret != CNCV_STATUS_SUCCESS) {
    printf("Exec cncvSyncQueue failed,  error code:%d\n", cncv_ret);
    return -1;
  }
  return 0;
}

int MluopResizeCvtDestroy(HANDLE h) {
  CvResizeCvtPrivate *d_ptr_ = static_cast<CvResizeCvtPrivate *>(h);
  if (d_ptr_->src_y_ptrs_cpu) {
    free(d_ptr_->src_y_ptrs_cpu);
    d_ptr_->src_y_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_uv_ptrs_cpu) {
    free(d_ptr_->src_uv_ptrs_cpu);
    d_ptr_->src_uv_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_y_ptrs_mlu) {
    cnrtFree(d_ptr_->src_y_ptrs_mlu);
    d_ptr_->src_y_ptrs_mlu = nullptr;
  }
  if (d_ptr_->src_uv_ptrs_mlu) {
    cnrtFree(d_ptr_->src_uv_ptrs_mlu);
    d_ptr_->src_uv_ptrs_mlu = nullptr;
  }
  if (d_ptr_->dst_ptrs_cpu) {
    free(d_ptr_->dst_ptrs_cpu);
    d_ptr_->dst_ptrs_cpu = nullptr;
  }
  if (d_ptr_->dst_ptrs_mlu) {
    cnrtFree(d_ptr_->dst_ptrs_mlu);
    d_ptr_->dst_ptrs_mlu = nullptr;
  }
  if (d_ptr_->workspace) {
    cnrtFree(d_ptr_->workspace);
    d_ptr_->workspace = nullptr;
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