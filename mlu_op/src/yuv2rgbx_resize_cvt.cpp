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

#if CNCV_MINOR < 8
extern cncvStatus_t cncvResizeConvert(cncvHandle_t handle,
                                      uint32_t batch_size,
                                      const cncvImageDescriptor *psrc_descs,
                                      const cncvRect *src_rois,
                                      void **src_y,
                                      void **src_uv,
                                      const cncvImageDescriptor *pdst_descs,
                                      const cncvRect *dst_rois,
                                      void **dst,
                                      const size_t workspace_size,
                                      void *workspace,
                                      cncvInterpolation interpolation);
#else
extern cncvStatus_t cncvResizeConvert_AdvancedROI(
                                      cncvHandle_t handle,
                                      const uint32_t batch_size,
                                      const cncvImageDescriptor *psrc_descs,
                                      const cncvRect *src_rois,
                                      void **src,
                                      const cncvImageDescriptor *pdst_descs,
                                      const cncvRect *dst_rois,
                                      void **dst,
                                      const size_t workspace_size,
                                      void *workspace,
                                      cncvInterpolation interpolation);
#endif

extern cncvStatus_t cncvGetResizeConvertWorkspaceSize(
    const uint32_t batch_size, const cncvImageDescriptor *psrc_descs,
    const cncvRect *src_rois, const cncvImageDescriptor *pdst_descs,
    const cncvRect *dst_rois, size_t *workspace_size);

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

  cncvHandle_t handle = nullptr;
  cnrtQueue_t queue = nullptr;
  size_t workspace_size = 0;
  void *workspace = nullptr;

  float sw_time = 0.0;
  float hw_time = 0.0;
  struct timeval end;
  struct timeval start;
  cnrtNotifier_t event_begin = nullptr;
  cnrtNotifier_t event_end   = nullptr;

  void **src_y_ptrs_cpu = nullptr;
  void **src_uv_ptrs_cpu = nullptr;
  void **src_y_ptrs_mlu = nullptr;
  void **src_uv_ptrs_mlu = nullptr;
  void **dst_ptrs_cpu = nullptr;
  void **dst_ptrs_mlu = nullptr;

  void **src_ptrs_cpu = nullptr;
  void **src_ptrs_mlu = nullptr;
};

/**
* will be deprecated from next release version
*/
int MluopResizeCvtInit(HANDLE *h, int src_width, int src_height, int dst_width,
                       int dst_height, const char *src_pix_fmt,
                       const char *dst_pix_fmt, const char *depth) {
  CvResizeCvtPrivate *d_ptr_ = new CvResizeCvtPrivate;
  MLUOP_RT_CHECK(cnrtCreateQueue(&d_ptr_->queue), "cnrtCreateQueue");
  MLUOP_CV_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_CV_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue), "cncvSetQueue");

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
  d_ptr_->dst_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));

#if CNCV_MINOR < 8
  d_ptr_->src_y_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));
  d_ptr_->src_uv_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));

  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_y_ptrs_mlu),
                     sizeof(char *)), "cnrtMalloc");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_uv_ptrs_mlu),
                     sizeof(char *)), "cnrtMalloc");
#else
  d_ptr_->src_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char*) * 2));
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_ptrs_mlu),
                     sizeof(char *) * 2), "cnrtMalloc");
#endif
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->dst_ptrs_mlu),
                     sizeof(void *)), "cnrtMalloc");
  cncvStatus_t cvret;
  MLUOP_CV_CHECK(cncvGetResizeConvertWorkspaceSize(
                                    d_ptr_->batch_size, &d_ptr_->src_desc,
                                    &d_ptr_->src_rois, &d_ptr_->dst_desc,
                                    &d_ptr_->dst_rois, &d_ptr_->workspace_size),
                                    "cncvGetResizeConvertWorkspaceSize");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->workspace),
                     d_ptr_->workspace_size), "cnrtMalloc");
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtCreateNotifier(&d_ptr_->event_begin), "cnrtCreateNotifier");
  #endif
  *h = static_cast<void *>(d_ptr_);

  return 0;
}

/**
* will be deprecated from next release version
*/
int MluopResizeCvtExec(HANDLE h, void *input_y, void *input_uv,
                       void *output_rgbx) {
  CvResizeCvtPrivate *d_ptr_ = static_cast<CvResizeCvtPrivate *>(h);
  if (nullptr == d_ptr_->queue) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  d_ptr_->dst_ptrs_cpu[0] = output_rgbx;
#if CNCV_MINOR < 8
  d_ptr_->src_y_ptrs_cpu[0] = input_y;
  d_ptr_->src_uv_ptrs_cpu[0] = input_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_y_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_y_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_uv_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
#else
  d_ptr_->src_ptrs_cpu[0] = input_y;
  d_ptr_->src_ptrs_cpu[1] = input_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_ptrs_cpu),
                     sizeof(char *) * 2, CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
#endif
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->dst_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue),
                  "cnrtPlaceNotifier");
  #endif

#if CNCV_MINOR < 8
  MLUOP_CV_CHECK(cncvResizeConvert(d_ptr_->handle,
                              1,
                              &d_ptr_->src_desc,
                              &d_ptr_->src_rois,
                              reinterpret_cast<void **>(d_ptr_->src_y_ptrs_mlu),
                              reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_mlu),
                              &d_ptr_->dst_desc,
                              &d_ptr_->dst_rois,
                              reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu),
                              d_ptr_->workspace_size,
                              d_ptr_->workspace,
                              CNCV_INTER_BILINEAR),
                              "cncvResizeConvert");
#else
  MLUOP_CV_CHECK(cncvResizeConvert_AdvancedROI(d_ptr_->handle,
                              1,
                              &d_ptr_->src_desc,
                              &d_ptr_->src_rois,
                              reinterpret_cast<void **>(d_ptr_->src_ptrs_mlu),
                              &d_ptr_->dst_desc,
                              &d_ptr_->dst_rois,
                              reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu),
                              d_ptr_->workspace_size,
                              d_ptr_->workspace,
                              CNCV_INTER_BILINEAR),
                              "cncvResizeConvert_AdvancedROI");
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue),
                "cnrtPlaceNotifier");
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
int MluopResizeCvtPadExec(HANDLE h, void *input_y, void *input_uv,
                          void *output_rgbx) {
  CvResizeCvtPrivate *d_ptr_ = static_cast<CvResizeCvtPrivate *>(h);
  const float EPSINON = 0.00001f;

  if (nullptr == d_ptr_->queue) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  d_ptr_->dst_ptrs_cpu[0] = output_rgbx;

#if CNCV_MONOR < 8
  d_ptr_->src_y_ptrs_cpu[0] = input_y;
  d_ptr_->src_uv_ptrs_cpu[0] = input_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_y_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_y_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV),
                     "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_uv_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV),
                     "cnrtMemcpy");
#else
  d_ptr_->src_ptrs_cpu[0] = input_y;
  d_ptr_->src_ptrs_cpu[1] = input_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_ptrs_cpu),
                     sizeof(char *) * 2, CNRT_MEM_TRANS_DIR_HOST2DEV),
                     "cnrtMemcpy");
#endif
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->dst_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV),
                     "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemset(output_rgbx, 0,
                     d_ptr_->dst_desc.stride[0] * d_ptr_->dst_desc.height),
                     "cnrtMemcpy");

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

  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue),
                "cnrtPlaceNotifier");
  #endif
#if CNCV_MONOR < 8
  MLUOP_CV_CHECK(cncvResizeConvert(d_ptr_->handle,
                              1,
                              &d_ptr_->src_desc,
                              &d_ptr_->src_rois,
                              reinterpret_cast<void **>(d_ptr_->src_y_ptrs_mlu),
                              reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_mlu),
                              &d_ptr_->dst_desc,
                              &d_ptr_->dst_rois,
                              reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu),
                              d_ptr_->workspace_size, d_ptr_->workspace,
                              CNCV_INTER_BILINEAR),
                              "cncvResizeConvert");
#else
  MLUOP_CV_CHECK(cncvResizeConvert_AdvancedROI(d_ptr_->handle,
                              1,
                              &d_ptr_->src_desc,
                              &d_ptr_->src_rois,
                              reinterpret_cast<void **>(d_ptr_->src_ptrs_mlu),
                              &d_ptr_->dst_desc,
                              &d_ptr_->dst_rois,
                              reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu),
                              d_ptr_->workspace_size, d_ptr_->workspace,
                              CNCV_INTER_BILINEAR),
                              "cncvResizeConvert_AdvancedROI");
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue),
                "cnrtPlaceNorifier");
  #endif
  MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  MLUOP_RT_CHECK(cnrtNotifierDuration(d_ptr_->event_begin, d_ptr_->event_end,
                          &d_ptr_->hw_time, "cnrtNotifierDuration");
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
int MluopResizeCvtDestroy(HANDLE h) {
  CvResizeCvtPrivate *d_ptr_ = static_cast<CvResizeCvtPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop resize cvt op not init\n");
    return 0;
  }
  #ifdef DEBUG
  if (d_ptr_->event_begin) {
    MLUOP_RT_CHECK(cnrtDestroyNotifier(&d_ptr_->event_begin),
                  "cnrtDestroyNotifier");
  }
  if (d_ptr_->event_end) {
    MLUOP_RT_CHECK(cnrtDestroyNotifier(&d_ptr_->event_end),
                  "cnrtDestroyNotifier");
  }
  #endif
  if (d_ptr_->src_y_ptrs_cpu) {
    free(d_ptr_->src_y_ptrs_cpu);
    d_ptr_->src_y_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_uv_ptrs_cpu) {
    free(d_ptr_->src_uv_ptrs_cpu);
    d_ptr_->src_uv_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_ptrs_cpu) {
    free(d_ptr_->src_ptrs_cpu);
    d_ptr_->src_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_y_ptrs_mlu) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_y_ptrs_mlu), "cnrtFree");
    d_ptr_->src_y_ptrs_mlu = nullptr;
  }
  if (d_ptr_->src_uv_ptrs_mlu) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_uv_ptrs_mlu), "cnrtFree");
    d_ptr_->src_uv_ptrs_mlu = nullptr;
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
  if (d_ptr_->workspace) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->workspace), "cnrtFree");
    d_ptr_->workspace = nullptr;
  }
  if (d_ptr_->queue) {
    MLUOP_RT_CHECK(cnrtDestroyQueue(d_ptr_->queue), "cnrtDestroyQueue");
    d_ptr_->queue = nullptr;
  }
  if (d_ptr_->handle) {
    MLUOP_CV_CHECK(cncvDestroy(d_ptr_->handle), "cncvDestroy");
    d_ptr_->handle = nullptr;
  }
  if (d_ptr_) {
    delete d_ptr_;
    d_ptr_ = nullptr;
  }

  return 0;
}

int mluOpResizeCvtInit(HANDLE *h, int src_width, int src_height, int dst_width,
                       int dst_height, const char *src_pix_fmt,
                       const char *dst_pix_fmt, const char *depth) {
  CvResizeCvtPrivate *d_ptr_ = new CvResizeCvtPrivate;
  MLUOP_RT_CHECK(cnrtCreateQueue(&d_ptr_->queue), "cnrtCreateQueue");
  MLUOP_CV_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_CV_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue), "cncvSetQueue");

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
  d_ptr_->dst_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));

#if CNCV_MINOR < 8
  d_ptr_->src_y_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));
  d_ptr_->src_uv_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char *)));

  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_y_ptrs_mlu),
                     sizeof(char *)), "cnrtMalloc");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_uv_ptrs_mlu),
                     sizeof(char *)), "cnrtMalloc");
#else
  d_ptr_->src_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char*) * 2));
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->src_ptrs_mlu),
                     sizeof(char *) * 2), "cnrtMalloc");
#endif
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->dst_ptrs_mlu),
                     sizeof(void *)), "cnrtMalloc");
  cncvStatus_t cvret;
  MLUOP_CV_CHECK(cncvGetResizeConvertWorkspaceSize(
                                    d_ptr_->batch_size, &d_ptr_->src_desc,
                                    &d_ptr_->src_rois, &d_ptr_->dst_desc,
                                    &d_ptr_->dst_rois, &d_ptr_->workspace_size),
                                    "cncvGetResizeConvertWorkspaceSize");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void **>(&d_ptr_->workspace),
                     d_ptr_->workspace_size), "cnrtMalloc");
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtCreateNotifier(&d_ptr_->event_begin), "cnrtCreateNotifier");
  #endif
  *h = static_cast<void *>(d_ptr_);

  return 0;
}

int mluOpResizeCvtExec(HANDLE h, void *input_y, void *input_uv,
                       void *output_rgbx) {
  CvResizeCvtPrivate *d_ptr_ = static_cast<CvResizeCvtPrivate *>(h);
  if (nullptr == d_ptr_->queue) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  d_ptr_->dst_ptrs_cpu[0] = output_rgbx;
#if CNCV_MINOR < 8
  d_ptr_->src_y_ptrs_cpu[0] = input_y;
  d_ptr_->src_uv_ptrs_cpu[0] = input_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_y_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_y_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_uv_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
#else
  d_ptr_->src_ptrs_cpu[0] = input_y;
  d_ptr_->src_ptrs_cpu[1] = input_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_ptrs_cpu),
                     sizeof(char *) * 2, CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
#endif
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->dst_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue),
                  "cnrtPlaceNotifier");
  #endif

#if CNCV_MINOR < 8
  MLUOP_CV_CHECK(cncvResizeConvert(d_ptr_->handle,
                              1,
                              &d_ptr_->src_desc,
                              &d_ptr_->src_rois,
                              reinterpret_cast<void **>(d_ptr_->src_y_ptrs_mlu),
                              reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_mlu),
                              &d_ptr_->dst_desc,
                              &d_ptr_->dst_rois,
                              reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu),
                              d_ptr_->workspace_size,
                              d_ptr_->workspace,
                              CNCV_INTER_BILINEAR),
                              "cncvResizeConvert");
#else
  MLUOP_CV_CHECK(cncvResizeConvert_AdvancedROI(d_ptr_->handle,
                              1,
                              &d_ptr_->src_desc,
                              &d_ptr_->src_rois,
                              reinterpret_cast<void **>(d_ptr_->src_ptrs_mlu),
                              &d_ptr_->dst_desc,
                              &d_ptr_->dst_rois,
                              reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu),
                              d_ptr_->workspace_size,
                              d_ptr_->workspace,
                              CNCV_INTER_BILINEAR),
                              "cncvResizeConvert_AdvancedROI");
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue),
                "cnrtPlaceNotifier");
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

int mluOpResizeCvtExecPad(HANDLE h, void *input_y, void *input_uv,
                          void *output_rgbx) {
  CvResizeCvtPrivate *d_ptr_ = static_cast<CvResizeCvtPrivate *>(h);
  const float EPSINON = 0.00001f;

  if (nullptr == d_ptr_->queue) {
    printf("Not create cnrt queue\n");
    return -1;
  }
  d_ptr_->dst_ptrs_cpu[0] = output_rgbx;

#if CNCV_MONOR < 8
  d_ptr_->src_y_ptrs_cpu[0] = input_y;
  d_ptr_->src_uv_ptrs_cpu[0] = input_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_y_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_y_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV),
                     "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_uv_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV),
                     "cnrtMemcpy");
#else
  d_ptr_->src_ptrs_cpu[0] = input_y;
  d_ptr_->src_ptrs_cpu[1] = input_uv;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->src_ptrs_cpu),
                     sizeof(char *) * 2, CNRT_MEM_TRANS_DIR_HOST2DEV),
                     "cnrtMemcpy");
#endif
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu,
                     reinterpret_cast<void **>(d_ptr_->dst_ptrs_cpu),
                     sizeof(char *), CNRT_MEM_TRANS_DIR_HOST2DEV),
                     "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemset(output_rgbx, 0,
                     d_ptr_->dst_desc.stride[0] * d_ptr_->dst_desc.height),
                     "cnrtMemcpy");

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

  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue),
                "cnrtPlaceNotifier");
  #endif
#if CNCV_MONOR < 8
  MLUOP_CV_CHECK(cncvResizeConvert(d_ptr_->handle,
                              1,
                              &d_ptr_->src_desc,
                              &d_ptr_->src_rois,
                              reinterpret_cast<void **>(d_ptr_->src_y_ptrs_mlu),
                              reinterpret_cast<void **>(d_ptr_->src_uv_ptrs_mlu),
                              &d_ptr_->dst_desc,
                              &d_ptr_->dst_rois,
                              reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu),
                              d_ptr_->workspace_size, d_ptr_->workspace,
                              CNCV_INTER_BILINEAR),
                              "cncvResizeConvert");
#else
  MLUOP_CV_CHECK(cncvResizeConvert_AdvancedROI(d_ptr_->handle,
                              1,
                              &d_ptr_->src_desc,
                              &d_ptr_->src_rois,
                              reinterpret_cast<void **>(d_ptr_->src_ptrs_mlu),
                              &d_ptr_->dst_desc,
                              &d_ptr_->dst_rois,
                              reinterpret_cast<void **>(d_ptr_->dst_ptrs_mlu),
                              d_ptr_->workspace_size, d_ptr_->workspace,
                              CNCV_INTER_BILINEAR),
                              "cncvResizeConvert_AdvancedROI");
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue),
                "cnrtPlaceNorifier");
  #endif
  MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  MLUOP_RT_CHECK(cnrtNotifierDuration(d_ptr_->event_begin, d_ptr_->event_end,
                          &d_ptr_->hw_time, "cnrtNotifierDuration");
  d_ptr_->sw_time = (d_ptr_->end.tv_sec - d_ptr_->start.tv_sec) * 1000000
                    + (d_ptr_->end.tv_usec - d_ptr_->start.tv_usec);
  printf("hw time: %.3f ms, sw time: %.3f ms\n",
        d_ptr_->hw_time/1000.f, d_ptr_->sw_time/1000.f);
  #endif

  return 0;
}

int mluOpResizeCvtDestroy(HANDLE h) {
  CvResizeCvtPrivate *d_ptr_ = static_cast<CvResizeCvtPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop resize cvt op not init\n");
    return 0;
  }
  #ifdef DEBUG
  if (d_ptr_->event_begin) {
    MLUOP_RT_CHECK(cnrtDestroyNotifier(&d_ptr_->event_begin),
                  "cnrtDestroyNotifier");
  }
  if (d_ptr_->event_end) {
    MLUOP_RT_CHECK(cnrtDestroyNotifier(&d_ptr_->event_end),
                  "cnrtDestroyNotifier");
  }
  #endif
  if (d_ptr_->src_y_ptrs_cpu) {
    free(d_ptr_->src_y_ptrs_cpu);
    d_ptr_->src_y_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_uv_ptrs_cpu) {
    free(d_ptr_->src_uv_ptrs_cpu);
    d_ptr_->src_uv_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_ptrs_cpu) {
    free(d_ptr_->src_ptrs_cpu);
    d_ptr_->src_ptrs_cpu = nullptr;
  }
  if (d_ptr_->src_y_ptrs_mlu) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_y_ptrs_mlu), "cnrtFree");
    d_ptr_->src_y_ptrs_mlu = nullptr;
  }
  if (d_ptr_->src_uv_ptrs_mlu) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_uv_ptrs_mlu), "cnrtFree");
    d_ptr_->src_uv_ptrs_mlu = nullptr;
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
  if (d_ptr_->workspace) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->workspace), "cnrtFree");
    d_ptr_->workspace = nullptr;
  }
  if (d_ptr_->queue) {
    MLUOP_RT_CHECK(cnrtDestroyQueue(d_ptr_->queue), "cnrtDestroyQueue");
    d_ptr_->queue = nullptr;
  }
  if (d_ptr_->handle) {
    MLUOP_CV_CHECK(cncvDestroy(d_ptr_->handle), "cncvDestroy");
    d_ptr_->handle = nullptr;
  }
  if (d_ptr_) {
    delete d_ptr_;
    d_ptr_ = nullptr;
  }

  return 0;
}
