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

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
extern cncvStatus_t cncvRgbxToYuv(cncvHandle_t handle,
                                  const cncvImageDescriptor src_desc,
                                  const cncvRect src_roi,
                                  const void *src,
                                  const cncvImageDescriptor dst_desc,
                                  void **dst);
#else
cncvStatus_t cncvRgbxToYuv_BasicROIP2(cncvHandle_t handle,
                                  const cncvImageDescriptor src_desc,
                                  const cncvRect src_roi,
                                  const void *src,
                                  const cncvImageDescriptor dst_desc,
                                  void *y,
                                  void *uv);
#endif

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

  float sw_time = 0.0;
  float hw_time = 0.0;
  struct timeval end;
  struct timeval start;
  cnrtNotifier_t event_begin = nullptr;
  cnrtNotifier_t event_end   = nullptr;

  cncvImageDescriptor src_desc;
  cncvImageDescriptor dst_desc;
  struct cncvRect     src_rois;

  void **dst_yuv_ptrs_cpu_ = nullptr;
  void **dst_yuv_ptrs_mlu_ = nullptr;
};  // CVResziePrivate

/**
* will be deprecated from next release version
*/
int mluop_convert_rgbx2yuv_init(HANDLE *h,
                                int width,
                                int height,
                                const char *src_pix_fmt,
                                const char *dst_pix_fmt,
                                const char *depth) {
  CVRGBX2YUVPrivate *d_ptr_ = new CVRGBX2YUVPrivate;
  MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue_), "mluQueueCreate");
  MLUOP_CV_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_CV_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue_), "cncvSetQueue");

  cnrtRet_t cnret;
  d_ptr_->dst_yuv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * 2));
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->dst_yuv_ptrs_mlu_),
                            2 * sizeof(char*)), "cnrtMalloc");
  d_ptr_->width = PAD_UP(width, ALIGN_R2Y_CVT);
  d_ptr_->height = height;
  d_ptr_->src_pix_fmt = getCNCVPixFmtFromPixindex(src_pix_fmt);
  d_ptr_->dst_pix_fmt = getCNCVPixFmtFromPixindex(dst_pix_fmt);
  d_ptr_->src_stride[0] = d_ptr_->width *
                          getSizeOfDepth(getCNCVDepthFromIndex(depth)) *
                          getPixFmtChannelNum(getCNCVPixFmtFromPixindex(src_pix_fmt));
  d_ptr_->dst_stride[0] = d_ptr_->width *
                          getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->dst_stride[1] = d_ptr_->width *
                          getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->depth = getCNCVDepthFromIndex(depth);

  #ifdef DEBUG
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_begin),"mluNotifierCreate");
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_end),  "mluNotifierCreate");
  #endif
  d_ptr_->src_desc = {d_ptr_->width, d_ptr_->height, {d_ptr_->src_stride[0]},
              d_ptr_->src_pix_fmt, d_ptr_->src_color_space, d_ptr_->depth};
  d_ptr_->dst_desc = {d_ptr_->width, d_ptr_->height,
              {d_ptr_->dst_stride[0], d_ptr_->dst_stride[1]},
              d_ptr_->dst_pix_fmt, d_ptr_->dst_color_space, d_ptr_->depth};
  d_ptr_->src_rois = {0, 0, d_ptr_->width, d_ptr_->height};

  *h = static_cast<void *>(d_ptr_);

  return 0;
}

/**
* will be deprecated from next release version
*/
int mluop_convert_rgbx2yuv_exec(HANDLE h,
                                void *input_rgbx,
                                void *output_y,
                                void *output_uv) {
  CVRGBX2YUVPrivate *d_ptr_ = static_cast<CVRGBX2YUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
    printf("Not create cnrt queue\n");
    return -1;
  }
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  d_ptr_->dst_yuv_ptrs_cpu_[0] = output_y;
  d_ptr_->dst_yuv_ptrs_cpu_[1] = output_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_yuv_ptrs_mlu_,
                    reinterpret_cast<void **>(d_ptr_->dst_yuv_ptrs_cpu_),
                    sizeof(char*) * 2,
                    CNRT_MEM_TRANS_DIR_HOST2DEV),
                    "cnrtMemcpy");
#endif
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNorifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvRgbxToYuv(d_ptr_->handle, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<const void *>(input_rgbx),
                            d_ptr_->dst_desc,
                            reinterpret_cast<void **>(d_ptr_->dst_yuv_ptrs_mlu_)),
                            "cncvRgbxToYuv");
#else
  MLUOP_CV_CHECK(cncvRgbxToYuv_BasicROIP2(d_ptr_->handle, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<const void *>(input_rgbx),
                            d_ptr_->dst_desc,
                            reinterpret_cast<void *>(output_y),
                            reinterpret_cast<void *>(output_uv)),
                            "cncvRbgxToYuv_BasicROI2");
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
                "cnrtPlaceNorifier");
  #endif
  MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  MLUOP_RT_CHECK(cnrtNotifierDuration(d_ptr_->event_begin,
                                      d_ptr_->event_end,
                                      &d_ptr_->hw_time),
                                      "cnrtNotifierDuration");
  d_ptr_->sw_time = (d_ptr_->end.tv_sec - d_ptr_->start.tv_sec) * 1000000
                    + (d_ptr_->end.tv_usec - d_ptr_->start.tv_usec);
  printf("hw time: %.3f ms, sw time: %.3f ms\n",
        d_ptr_->hw_time/1000.f, d_ptr_->sw_time/1000.f);
  #endif

  return 0;
}

/**
* will be deprecated from next release version
*/
int mluop_convert_rgbx2yuv_exec_roi(HANDLE h,
                                    void *input_rgbx,
                                    void *output_y,
                                    void *output_uv,
                                    uint32_t in_x,
                                    uint32_t in_y,
                                    uint32_t in_w,
                                    uint32_t in_h) {
  CVRGBX2YUVPrivate *d_ptr_ = static_cast<CVRGBX2YUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  struct cncvRect src_rois = {in_x, in_y, in_w, in_h};

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  d_ptr_->dst_yuv_ptrs_cpu_[0] = output_y;
  d_ptr_->dst_yuv_ptrs_cpu_[1] = output_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_yuv_ptrs_mlu_,
                      reinterpret_cast<void **>(d_ptr_->dst_yuv_ptrs_cpu_),
                      sizeof(char*) * 2,
                      CNRT_MEM_TRANS_DIR_HOST2DEV),
                      "cnrtMemcpy");
#endif

  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvRgbxToYuv(d_ptr_->handle,
                d_ptr_->src_desc,
                src_rois,
                reinterpret_cast<const void *>(input_rgbx),
                d_ptr_->dst_desc,
                reinterpret_cast<void **>(d_ptr_->dst_yuv_ptrs_mlu_)),
                "cncvRgbxToYuv");
#else
  MLUOP_CV_CHECK(cncvRgbxToYuv_BasicROIP2(d_ptr_->handle,
                d_ptr_->src_desc,
                src_rois,
                reinterpret_cast<const void *>(input_rgbx),
                d_ptr_->dst_desc,
                reinterpret_cast<void *>(output_y),
                reinterpret_cast<void *>(output_uv)),
                "cncvRgbxToYuv_BasicROIP2");
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
  MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  MLUOP_RT_CHECK(cnrtNotifierDuration(d_ptr_->event_begin,
                                      d_ptr_->event_end,
                                      &d_ptr_->hw_time),
                                      "cnrtNotifierDuration");
  d_ptr_->sw_time = (d_ptr_->end.tv_sec - d_ptr_->start.tv_sec) * 1000000
                    + (d_ptr_->end.tv_usec - d_ptr_->start.tv_usec);
  printf("hw time: %.3f ms, sw time: %.3f ms\n",
        d_ptr_->hw_time/1000.f, d_ptr_->sw_time/1000.f);
  #endif

  return 0;
}

/**
* will be deprecated from next release version
*/
int mluop_convert_rgbx2yuv_destroy(HANDLE h) {
  CVRGBX2YUVPrivate *d_ptr_ = static_cast<CVRGBX2YUVPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop cvt rgbx2yuv op not init\n");
    return 0;
  }
  #ifdef DEBUG
  if (d_ptr_->event_begin) {
    MLUOP_RT_CHECK(mluNotifierDestory(&d_ptr_->event_begin),
                  "mluNotifierDestory");
  }
  if (d_ptr_->event_end) {
    MLUOP_RT_CHECK(mluNotifierDestory(&d_ptr_->event_end),
                  "mluNotifierDestory");
  }
  #endif
  if (d_ptr_->dst_yuv_ptrs_cpu_) {
    free(d_ptr_->dst_yuv_ptrs_cpu_);
    d_ptr_->dst_yuv_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_yuv_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->dst_yuv_ptrs_mlu_), "cnrtFree");
    d_ptr_->dst_yuv_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->queue_) {
    MLUOP_RT_CHECK(mluQueueDestroy(d_ptr_->queue_), "mluQueueDestroy");
    d_ptr_->queue_ = nullptr;
  }
  if (d_ptr_->handle) {
    MLUOP_CV_CHECK(cncvDestroy(d_ptr_->handle), "cncvDestroy");
    d_ptr_->handle = nullptr;
  }
  delete d_ptr_;
  d_ptr_ = nullptr;

  return 0;
}

int mluOpConvertRgbx2YuvInit(HANDLE *h,
                                int width,
                                int height,
                                const char *src_pix_fmt,
                                const char *dst_pix_fmt,
                                const char *depth) {
  CVRGBX2YUVPrivate *d_ptr_ = new CVRGBX2YUVPrivate;
  MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue_), "mluQueueCreate");
  MLUOP_CV_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_CV_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue_), "cncvSetQueue");

  cnrtRet_t cnret;
  d_ptr_->dst_yuv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * 2));
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->dst_yuv_ptrs_mlu_),
                            2 * sizeof(char*)), "cnrtMalloc");
  d_ptr_->width = PAD_UP(width, ALIGN_R2Y_CVT);
  d_ptr_->height = height;
  d_ptr_->src_pix_fmt = getCNCVPixFmtFromPixindex(src_pix_fmt);
  d_ptr_->dst_pix_fmt = getCNCVPixFmtFromPixindex(dst_pix_fmt);
  d_ptr_->src_stride[0] = d_ptr_->width *
                          getSizeOfDepth(getCNCVDepthFromIndex(depth)) *
                          getPixFmtChannelNum(getCNCVPixFmtFromPixindex(src_pix_fmt));
  d_ptr_->dst_stride[0] = d_ptr_->width *
                          getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->dst_stride[1] = d_ptr_->width *
                          getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->depth = getCNCVDepthFromIndex(depth);

  #ifdef DEBUG
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_begin),"mluNotifierCreate");
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_end),  "cnrtCreateNorifier");
  #endif
  d_ptr_->src_desc = {d_ptr_->width, d_ptr_->height, {d_ptr_->src_stride[0]},
              d_ptr_->src_pix_fmt, d_ptr_->src_color_space, d_ptr_->depth};
  d_ptr_->dst_desc = {d_ptr_->width, d_ptr_->height,
              {d_ptr_->dst_stride[0], d_ptr_->dst_stride[1]},
              d_ptr_->dst_pix_fmt, d_ptr_->dst_color_space, d_ptr_->depth};
  d_ptr_->src_rois = {0, 0, d_ptr_->width, d_ptr_->height};

  *h = static_cast<void *>(d_ptr_);

  return 0;
}

int mluOpConvertRgbx2YuvExec(HANDLE h,
                                void *input_rgbx,
                                void *output_y,
                                void *output_uv) {
  CVRGBX2YUVPrivate *d_ptr_ = static_cast<CVRGBX2YUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
    printf("Not create cnrt queue\n");
    return -1;
  }
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  d_ptr_->dst_yuv_ptrs_cpu_[0] = output_y;
  d_ptr_->dst_yuv_ptrs_cpu_[1] = output_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_yuv_ptrs_mlu_,
                    reinterpret_cast<void **>(d_ptr_->dst_yuv_ptrs_cpu_),
                    sizeof(char*) * 2,
                    CNRT_MEM_TRANS_DIR_HOST2DEV),
                    "cnrtMemcpy");
#endif
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNorifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvRgbxToYuv(d_ptr_->handle, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<const void *>(input_rgbx),
                            d_ptr_->dst_desc,
                            reinterpret_cast<void **>(d_ptr_->dst_yuv_ptrs_mlu_)),
                            "cncvRgbxToYuv");
#else
  MLUOP_CV_CHECK(cncvRgbxToYuv_BasicROIP2(d_ptr_->handle, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<const void *>(input_rgbx),
                            d_ptr_->dst_desc,
                            reinterpret_cast<void *>(output_y),
                            reinterpret_cast<void *>(output_uv)),
                            "cncvRbgxToYuv_BasicROI2");
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
                "cnrtPlaceNorifier");
  #endif
  MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  MLUOP_RT_CHECK(cnrtNotifierDuration(d_ptr_->event_begin,
                                      d_ptr_->event_end,
                                      &d_ptr_->hw_time),
                                      "cnrtNotifierDuration");
  d_ptr_->sw_time = (d_ptr_->end.tv_sec - d_ptr_->start.tv_sec) * 1000000
                    + (d_ptr_->end.tv_usec - d_ptr_->start.tv_usec);
  printf("hw time: %.3f ms, sw time: %.3f ms\n",
        d_ptr_->hw_time/1000.f, d_ptr_->sw_time/1000.f);
  #endif

  return 0;
}

int mluOpConvertRgbx2YuvExecRoi(HANDLE h,
                                    void *input_rgbx,
                                    void *output_y,
                                    void *output_uv,
                                    uint32_t in_x,
                                    uint32_t in_y,
                                    uint32_t in_w,
                                    uint32_t in_h) {
  CVRGBX2YUVPrivate *d_ptr_ = static_cast<CVRGBX2YUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  struct cncvRect src_rois = {in_x, in_y, in_w, in_h};

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  d_ptr_->dst_yuv_ptrs_cpu_[0] = output_y;
  d_ptr_->dst_yuv_ptrs_cpu_[1] = output_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_yuv_ptrs_mlu_,
                      reinterpret_cast<void **>(d_ptr_->dst_yuv_ptrs_cpu_),
                      sizeof(char*) * 2,
                      CNRT_MEM_TRANS_DIR_HOST2DEV),
                      "cnrtMemcpy");
#endif

  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvRgbxToYuv(d_ptr_->handle,
                d_ptr_->src_desc,
                src_rois,
                reinterpret_cast<const void *>(input_rgbx),
                d_ptr_->dst_desc,
                reinterpret_cast<void **>(d_ptr_->dst_yuv_ptrs_mlu_)),
                "cncvRgbxToYuv");
#else
  MLUOP_CV_CHECK(cncvRgbxToYuv_BasicROIP2(d_ptr_->handle,
                d_ptr_->src_desc,
                src_rois,
                reinterpret_cast<const void *>(input_rgbx),
                d_ptr_->dst_desc,
                reinterpret_cast<void *>(output_y),
                reinterpret_cast<void *>(output_uv)),
                "cncvRgbxToYuv_BasicROIP2");
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
  MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  MLUOP_RT_CHECK(cnrtNotifierDuration(d_ptr_->event_begin,
                                      d_ptr_->event_end,
                                      &d_ptr_->hw_time),
                                      "cnrtNotifierDuration");
  d_ptr_->sw_time = (d_ptr_->end.tv_sec - d_ptr_->start.tv_sec) * 1000000
                    + (d_ptr_->end.tv_usec - d_ptr_->start.tv_usec);
  printf("hw time: %.3f ms, sw time: %.3f ms\n",
        d_ptr_->hw_time/1000.f, d_ptr_->sw_time/1000.f);
  #endif

  return 0;
}

int mluOpConvertRgbx2YuvDestroy(HANDLE h) {
  CVRGBX2YUVPrivate *d_ptr_ = static_cast<CVRGBX2YUVPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop cvt rgbx2yuv op not init\n");
    return 0;
  }
  #ifdef DEBUG
  if (d_ptr_->event_begin) {
    MLUOP_RT_CHECK(mluNotifierDestory(&d_ptr_->event_begin),
                  "mluNotifierDestory");
  }
  if (d_ptr_->event_end) {
    MLUOP_RT_CHECK(mluNotifierDestory(&d_ptr_->event_end),
                  "mluNotifierDestory");
  }
  #endif
  if (d_ptr_->dst_yuv_ptrs_cpu_) {
    free(d_ptr_->dst_yuv_ptrs_cpu_);
    d_ptr_->dst_yuv_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_yuv_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->dst_yuv_ptrs_mlu_), "cnrtFree");
    d_ptr_->dst_yuv_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->queue_) {
    MLUOP_RT_CHECK(mluQueueDestroy(d_ptr_->queue_), "mluQueueDestroy");
    d_ptr_->queue_ = nullptr;
  }
  if (d_ptr_->handle) {
    MLUOP_CV_CHECK(cncvDestroy(d_ptr_->handle), "cncvDestroy");
    d_ptr_->handle = nullptr;
  }
  delete d_ptr_;
  d_ptr_ = nullptr;

  return 0;
}
