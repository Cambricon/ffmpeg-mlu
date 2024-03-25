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

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
extern cncvStatus_t cncvRgbxToRgbx(cncvHandle_t handle,
                                    const uint32_t batch_size,
                                    const cncvImageDescriptor src_desc,
                                    const cncvRect src_roi,
                                    void **src,
                                    const cncvImageDescriptor dst_desc,
                                    const cncvRect dst_roi,
                                    void **dst);
#else
#if CNCV_MAJOR >= 2
extern cncvStatus_t cncvRgbxToRgbx_ROI(cncvHandle_t handle,
                                    const uint32_t batch_size,
                                    const cncvImageDescriptor src_desc,
                                    const cncvRect src_roi,
                                    cncvBufferList_t src,
                                    const cncvImageDescriptor dst_desc,
                                    const cncvRect dst_roi,
                                    cncvBufferList_t dst);
#else
extern cncvStatus_t cncvRgbxToRgbx_ROI(cncvHandle_t handle,
                                    const uint32_t batch_size,
                                    const cncvImageDescriptor src_desc,
                                    const cncvRect src_roi,
                                    void **src,
                                    const cncvImageDescriptor dst_desc,
                                    const cncvRect dst_roi,
                                    void **dst);
#endif
#endif
extern cncvStatus_t cncvGetResizeRgbxWorkspaceSize(const uint32_t batch_size,
                                                   size_t *workspace_size);

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

/**
* will be deprecated from next release version
*/
int MluopConvertRgbx2RgbxInit(HANDLE *h, int width, int height,
                              const char *src_pix_fmt, const char *dst_pix_fmt,
                              const char *depth) {
  CvRgbx2RgbxPrivate *d_ptr_ = new CvRgbx2RgbxPrivate;
  MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue), "mluQueueCreate");
  MLUOP_RT_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_RT_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue), "cncvSetQueue");

  d_ptr_->width = PAD_UP(width, ALIGN_Y2R_CVT);
  d_ptr_->src_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));
  d_ptr_->dst_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_ptrs_mlu),
                            sizeof(char *)), "cnrtMalloc");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->dst_ptrs_mlu),
                            sizeof(void *)), "cnrtMalloc");
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
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_begin),"mluNotifierCreate");
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_end),  "mluNotifierCreate");
  #endif

  *h = static_cast<void *>(d_ptr_);

  return 0;
}

int mluOpConvertRgbx2RgbxInit(HANDLE *h, int width, int height,
                              const char *src_pix_fmt, const char *dst_pix_fmt,
                              const char *depth) {
  CvRgbx2RgbxPrivate *d_ptr_ = new CvRgbx2RgbxPrivate;
  MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue), "mluQueueCreate");
  MLUOP_RT_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_RT_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue), "cncvSetQueue");

  d_ptr_->width = PAD_UP(width, ALIGN_Y2R_CVT);
  d_ptr_->src_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));
  d_ptr_->dst_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_ptrs_mlu),
                            sizeof(char *)), "cnrtMalloc");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->dst_ptrs_mlu),
                            sizeof(void *)), "cnrtMalloc");
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
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_begin),"mluNotifierCreate");
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_end),  "mluNotifierCreate");
  #endif

  *h = static_cast<void *>(d_ptr_);

  return 0;
}

/**
* will be deprecated from next release version
*/
int MluopConvertRgbx2RgbxExec(HANDLE h, void *input_rgbx, void *output_rgbx) {
  CvRgbx2RgbxPrivate *d_ptr_ = static_cast<CvRgbx2RgbxPrivate *>(h);
  if (nullptr == d_ptr_->queue) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  d_ptr_->src_ptrs_cpu[0] = input_rgbx;
  d_ptr_->dst_ptrs_cpu[0] = output_rgbx;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu,
                            reinterpret_cast<void **>(d_ptr_->src_ptrs_cpu),
                            sizeof(char *),
                            CNRT_MEM_TRANS_DIR_HOST2DEV),
                            "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu,
                            reinterpret_cast<void **>(d_ptr_->dst_ptrs_cpu),
                            sizeof(char *),
                            CNRT_MEM_TRANS_DIR_HOST2DEV),
                            "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue),
                "cnrtPlaceNotifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvRgbxToRgbx(d_ptr_->handle,
                            1, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<void **>(d_ptr_->src_ptrs_mlu),
                            d_ptr_->dst_desc,
                            d_ptr_->dst_rois,
                            reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu)),
                            "cncvRgbxToRgbx");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvRgbxToRgbx_ROI(d_ptr_->handle,
                            1, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<cncvBufferList_t>(d_ptr_->src_ptrs_mlu),
                            d_ptr_->dst_desc,
                            d_ptr_->dst_rois,
                            reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_ptrs_mlu)),
                            "cncvRgbxToRgbx_ROI");
#else
  MLUOP_CV_CHECK(cncvRgbxToRgbx_ROI(d_ptr_->handle,
                            1, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<void **>(d_ptr_->src_ptrs_mlu),
                            d_ptr_->dst_desc,
                            d_ptr_->dst_rois,
                            reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu)),
                            "cncvRgbxToRgbx_ROI");
#endif
#endif
  #ifdef DEBUG
  cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue);
  #endif
  MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  MLUOP_RT_CHECK(cnrtNotifierDuration(d_ptr_->event_begin, d_ptr_->event_end,
                                    &d_ptr_->hw_time), "cnrtNotifierDuration");
  d_ptr_->sw_time = (d_ptr_->end.tv_sec - d_ptr_->start.tv_sec) * 1000000
                    + (d_ptr_->end.tv_usec - d_ptr_->start.tv_usec);
  printf("hw time: %.3f ms, sw time: %.3f ms\n",
        d_ptr_->hw_time/1000.f, d_ptr_->sw_time/1000.f);
  #endif

  return 0;
}

int mluOpConvertRgbx2RgbxExec(HANDLE h, void *input_rgbx, void *output_rgbx) {
  CvRgbx2RgbxPrivate *d_ptr_ = static_cast<CvRgbx2RgbxPrivate *>(h);
  if (nullptr == d_ptr_->queue) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  d_ptr_->src_ptrs_cpu[0] = input_rgbx;
  d_ptr_->dst_ptrs_cpu[0] = output_rgbx;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu,
                            reinterpret_cast<void **>(d_ptr_->src_ptrs_cpu),
                            sizeof(char *),
                            CNRT_MEM_TRANS_DIR_HOST2DEV),
                            "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu,
                            reinterpret_cast<void **>(d_ptr_->dst_ptrs_cpu),
                            sizeof(char *),
                            CNRT_MEM_TRANS_DIR_HOST2DEV),
                            "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue),
                "cnrtPlaceNotifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvRgbxToRgbx(d_ptr_->handle,
                            1, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<void **>(d_ptr_->src_ptrs_mlu),
                            d_ptr_->dst_desc,
                            d_ptr_->dst_rois,
                            reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu)),
                            "cncvRgbxToRgbx");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvRgbxToRgbx_ROI(d_ptr_->handle,
                            1, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<cncvBufferList_t>(d_ptr_->src_ptrs_mlu),
                            d_ptr_->dst_desc,
                            d_ptr_->dst_rois,
                            reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_ptrs_mlu)),
                            "cncvRgbxToRgbx_ROI");
#else
  MLUOP_CV_CHECK(cncvRgbxToRgbx_ROI(d_ptr_->handle,
                            1, d_ptr_->src_desc,
                            d_ptr_->src_rois,
                            reinterpret_cast<void **>(d_ptr_->src_ptrs_mlu),
                            d_ptr_->dst_desc,
                            d_ptr_->dst_rois,
                            reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu)),
                            "cncvRgbxToRgbx_ROI");
#endif
#endif
  #ifdef DEBUG
  cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue);
  #endif
  MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  MLUOP_RT_CHECK(cnrtNotifierDuration(d_ptr_->event_begin, d_ptr_->event_end,
                                    &d_ptr_->hw_time), "cnrtNotifierDuration");
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
int MluopConvertRgbx2RgbxDestroy(HANDLE h) {
  CvRgbx2RgbxPrivate *d_ptr_ = static_cast<CvRgbx2RgbxPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop cvt rgbx2rgbx op not init\n");
    return 0;
  }
  #ifdef DEBUG
  if (d_ptr_->event_begin) {
    MLUOP_RT_CHECK(mluNotifierDestroy(d_ptr_->event_begin),
                  "mluNotifierDestroy");
  }
  if (d_ptr_->event_end) {
    MLUOP_RT_CHECK(mluNotifierDestroy(d_ptr_->event_end),
                  "mluNotifierDestroy");
  }
  #endif
  if (d_ptr_->src_ptrs_cpu) {
    free(d_ptr_->src_ptrs_cpu);
    d_ptr_->src_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_ptrs_mlu) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_ptrs_mlu), "cnrtFree");
    d_ptr_->src_ptrs_mlu = nullptr;
  }
  if (d_ptr_->dst_ptrs_cpu) {
    free(d_ptr_->dst_ptrs_cpu);
    d_ptr_->dst_ptrs_cpu = nullptr;
  }
  if (d_ptr_->dst_ptrs_mlu) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->dst_ptrs_mlu), "cnrtFree");
    d_ptr_->dst_ptrs_mlu = nullptr;
  }
  if (d_ptr_->queue) {
    MLUOP_RT_CHECK(mluQueueDestroy(d_ptr_->queue), "mluQueueDestroy");
    d_ptr_->queue = nullptr;
  }
  if (d_ptr_->handle) {
    MLUOP_CV_CHECK(cncvDestroy(d_ptr_->handle), "cncvDestroy");
    d_ptr_->handle = nullptr;
  }
  delete d_ptr_;
  d_ptr_ = nullptr;

  return 0;
}

int mluOpConvertRgbx2RgbxDestroy(HANDLE h) {
  CvRgbx2RgbxPrivate *d_ptr_ = static_cast<CvRgbx2RgbxPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop cvt rgbx2rgbx op not init\n");
    return 0;
  }
  #ifdef DEBUG
  if (d_ptr_->event_begin) {
    MLUOP_RT_CHECK(mluNotifierDestroy(d_ptr_->event_begin),
                  "mluNotifierDestroy");
  }
  if (d_ptr_->event_end) {
    MLUOP_RT_CHECK(mluNotifierDestroy(d_ptr_->event_end),
                  "mluNotifierDestroy");
  }
  #endif
  if (d_ptr_->src_ptrs_cpu) {
    free(d_ptr_->src_ptrs_cpu);
    d_ptr_->src_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_ptrs_mlu) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_ptrs_mlu), "cnrtFree");
    d_ptr_->src_ptrs_mlu = nullptr;
  }
  if (d_ptr_->dst_ptrs_cpu) {
    free(d_ptr_->dst_ptrs_cpu);
    d_ptr_->dst_ptrs_cpu = nullptr;
  }
  if (d_ptr_->dst_ptrs_mlu) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->dst_ptrs_mlu), "cnrtFree");
    d_ptr_->dst_ptrs_mlu = nullptr;
  }
  if (d_ptr_->queue) {
    MLUOP_RT_CHECK(mluQueueDestroy(d_ptr_->queue), "mluQueueDestroy");
    d_ptr_->queue = nullptr;
  }
  if (d_ptr_->handle) {
    MLUOP_CV_CHECK(cncvDestroy(d_ptr_->handle), "cncvDestroy");
    d_ptr_->handle = nullptr;
  }
  delete d_ptr_;
  d_ptr_ = nullptr;

  return 0;
}
