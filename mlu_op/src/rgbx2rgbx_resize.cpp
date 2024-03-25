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

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
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
#else
#if CNCV_MAJOR >= 2
cncvStatus_t cncvResizeRgbx_AdvancedROI(cncvHandle_t handle,
                                        uint32_t batch_size,
                                        const cncvImageDescriptor *psrc_descs,
                                        const cncvRect *src_rois,
                                        cncvBufferList_t src,
                                        const cncvImageDescriptor *pdst_descs,
                                        const cncvRect *dst_rois,
                                        cncvBufferList_t dst,
                                        const size_t workspace_size,
                                        void *workspace,
                                        cncvInterpolation interpolation);
cncvStatus_t cncvGetResizeRgbxAdvancedWorkspaceSize(const uint32_t batch_size,
                                        const cncvImageDescriptor *psrc_descs,
                                        const cncvRect *psrc_rois,
                                        const cncvImageDescriptor *pdst_descs,
                                        const cncvRect *pdst_rois,
                                        const cncvInterpolation interpolation,
                                        size_t *workspace_size);
#else
cncvStatus_t cncvResizeRgbx_AdvancedROI(cncvHandle_t handle,
                                        uint32_t batch_size,
                                        const cncvImageDescriptor *psrc_descs,
                                        const cncvRect *src_rois,
                                        void **src,
                                        const cncvImageDescriptor *pdst_descs,
                                        const cncvRect *dst_rois,
                                        void **dst,
                                        const size_t workspace_size,
                                        void *workspace,
                                        cncvInterpolation interpolation);
extern cncvStatus_t cncvGetResizeRgbxWorkspaceSize(const uint32_t batch_size,
                                                   size_t *workspace_size);
#endif
#endif

struct CVResizeRgbxPrivate {
 public:
  uint32_t input_w, input_h;
  uint32_t input_stride_in_bytes;
  uint32_t output_w, output_h;
  uint32_t output_stride_in_bytes;

  uint32_t batch_size;
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

  float sw_time = 0.0;
  float hw_time = 0.0;
  struct timeval end;
  struct timeval start;
  cnrtNotifier_t event_begin = nullptr;
  cnrtNotifier_t event_end   = nullptr;

  std::deque<void*> src_rgbx_ptrs_cache_;
  void **src_rgbx_ptrs_cpu_ = nullptr;
  void **src_rgbx_ptrs_mlu_ = nullptr;
  std::deque<void*> dst_rgbx_ptrs_cache_;
  void **dst_rgbx_ptrs_cpu_ = nullptr;
  void **dst_rgbx_ptrs_mlu_ = nullptr;
};

/**
* will be deprecated from next release version
*/
int mluop_resize_rgbx_init(HANDLE *h,
                           int input_w,
                           int input_h,
                           int output_w,
                           int output_h,
                           const char *pix_fmt,
                           const char *depth) {
  CVResizeRgbxPrivate *d_ptr_ = new CVResizeRgbxPrivate;
  MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue_), "mluQueueCreate");
  MLUOP_CV_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_CV_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue_), "cncvSetQueue");

  d_ptr_->batch_size = 1;
  d_ptr_->input_h = input_h;
  d_ptr_->input_w = PAD_UP(input_w, ALIGN_R_SCALE);
  d_ptr_->output_h = output_h;
  d_ptr_->output_w = PAD_UP(output_w, ALIGN_R_SCALE);

  d_ptr_->depth_size = getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->pix_chn_num = getPixFmtChannelNum(getCNCVPixFmtFromPixindex(pix_fmt));
  d_ptr_->input_stride_in_bytes = d_ptr_->input_w *
                                  d_ptr_->pix_chn_num * d_ptr_->depth_size;
  d_ptr_->output_stride_in_bytes = d_ptr_->output_w *
                                  d_ptr_->pix_chn_num * d_ptr_->depth_size;
  d_ptr_->src_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) *
                                  d_ptr_->batch_size));
  d_ptr_->dst_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) *
                                  d_ptr_->batch_size));
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_rgbx_ptrs_mlu_),
                                  sizeof(char*) * d_ptr_->batch_size),
                                  "cnrtMalloc");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_rgbx_ptrs_mlu_),
                                  sizeof(char*) * d_ptr_->batch_size),
                                  "cnrtMalloc");

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

  #ifdef DEBUG
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_begin),"mluNotifierCreate");
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_end),  "mluNotifierCreate");
  #endif

#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvGetResizeRgbxAdvancedWorkspaceSize(
                                      d_ptr_->batch_size,
                                      &d_ptr_->src_desc,
                                      &d_ptr_->src_rois,
                                      &d_ptr_->dst_desc,
                                      &d_ptr_->dst_rois,
                                      CNCV_INTER_BILINEAR,
                                      &d_ptr_->workspace_size),
                                      "cncvGetResizeRgbxAdvancedWorkspaceSize");
#else
  MLUOP_CV_CHECK(cncvGetResizeRgbxWorkspaceSize(d_ptr_->batch_size,
                                  &d_ptr_->workspace_size),
                                  "cncvGetResizeRgbxWorkspaceSize");
#endif
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->workspace),
                                  d_ptr_->workspace_size), "cnrtMalloc");

  *h = static_cast<void *>(d_ptr_);

  return 0;
}

/**
* will be deprecated from next release version
*/
int mluop_resize_rgbx_exec(HANDLE h, void *input, void *output) {
  CVResizeRgbxPrivate *d_ptr_ = static_cast<CVResizeRgbxPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     printf("Sync queue is null.");
     return -1;
  }
  for (uint32_t bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_rgbx_ptrs_cpu_[bi] = input;
    d_ptr_->dst_rgbx_ptrs_cpu_[bi] = output;
  }
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_rgbx_ptrs_mlu_,
              reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_cpu_),
              sizeof(char*) * d_ptr_->batch_size,
              CNRT_MEM_TRANS_DIR_HOST2DEV),
              "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_,
              reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
              sizeof(char*) * d_ptr_->batch_size,
              CNRT_MEM_TRANS_DIR_HOST2DEV),
              "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeRgbx(d_ptr_->handle,
                 d_ptr_->batch_size,
                 d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#endif
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
                "cnrtPlaceNorifier");
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
int mluop_resize_pad_rgbx_exec(HANDLE h,
                               void *input, void *output) {
  CVResizeRgbxPrivate *d_ptr_ = static_cast<CVResizeRgbxPrivate *>(h);
  const float EPSINON = 0.00001f;

  if (nullptr == d_ptr_->queue_) {
     printf("Sync queue is null.");
     return -1;
  }

  for (uint32_t bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_rgbx_ptrs_cpu_[bi] = input;
    d_ptr_->dst_rgbx_ptrs_cpu_[bi] = output;
  }

  cnrtRet_t cnret;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_rgbx_ptrs_mlu_,
                          reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_cpu_),
                          sizeof(char*) * d_ptr_->batch_size,
                          CNRT_MEM_TRANS_DIR_HOST2DEV),
                          "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_,
                          reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
                          sizeof(char*) * d_ptr_->batch_size,
                          CNRT_MEM_TRANS_DIR_HOST2DEV),
                          "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemset(output, 0,
                          d_ptr_->dst_desc.stride[0] * d_ptr_->dst_desc.height),
                          "cnrtMemset");
  int low_bound_p, low_bound_len;
  float src_scale = (float) d_ptr_->src_desc.width / d_ptr_->src_desc.height;
  float dst_scale = (float) d_ptr_->dst_desc.width / d_ptr_->dst_desc.height;
  float dis = fabs(src_scale - dst_scale);
  if (dis > EPSINON) {
    if(src_scale < dst_scale) {
      d_ptr_->dst_rois.y = 0;
      d_ptr_->dst_rois.h = d_ptr_->dst_desc.height;
      low_bound_len = (d_ptr_->dst_desc.height * d_ptr_->src_desc.width / d_ptr_->src_desc.height) / 2;
      d_ptr_->dst_rois.w = low_bound_len * 2;
      low_bound_p = (d_ptr_->dst_desc.width - d_ptr_->dst_rois.w) / 4;
      d_ptr_->dst_rois.x = low_bound_p * 2;
    } else {
      d_ptr_->dst_rois.x = 0;
      d_ptr_->dst_rois.w = d_ptr_->dst_desc.width;
      low_bound_len = (d_ptr_->dst_desc.width * d_ptr_->src_desc.height / d_ptr_->src_desc.width) / 2;
      d_ptr_->dst_rois.h = low_bound_len * 2;
      low_bound_p = (d_ptr_->dst_desc.height - d_ptr_->dst_rois.h) / 4;
      d_ptr_->dst_rois.y = low_bound_p * 2;
    }
  }
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeRgbx(d_ptr_->handle,
                 d_ptr_->batch_size,
                 d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#endif
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
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
int mluop_resize_roi_rgbx_exec(HANDLE h,
                            void *input, void *output,
                            uint32_t src_roi_x, uint32_t src_roi_y,
                            uint32_t src_roi_w, uint32_t src_roi_h,
                            uint32_t dst_roi_x, uint32_t dst_roi_y,
                            uint32_t dst_roi_w, uint32_t dst_roi_h) {
  CVResizeRgbxPrivate *d_ptr_ = static_cast<CVResizeRgbxPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
    printf("Sync queue is null.\n");
    return -1;
  }
  if (src_roi_x%2 || src_roi_y%2 || src_roi_w%2 || src_roi_h%2 ||
      dst_roi_x%2 || dst_roi_y%2 || dst_roi_w%2 || dst_roi_h%2) {
    printf("roi params illegal, value must be even.\n");
    return -1;
  }
  if ((src_roi_x + src_roi_w > d_ptr_->src_desc.width)  ||
      (src_roi_y + src_roi_h > d_ptr_->src_desc.height) ||
      (dst_roi_x + dst_roi_w > d_ptr_->dst_desc.width ) ||
      (dst_roi_y + dst_roi_h > d_ptr_->dst_desc.height)) {
    printf("roi params illegal, the ROI area must be within the image scope.\n");
    return -1;
  }
  for (uint32_t bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_rgbx_ptrs_cpu_[bi] = input;
    d_ptr_->dst_rgbx_ptrs_cpu_[bi] = output;
  }
  d_ptr_->src_rois.x = src_roi_x;
  d_ptr_->src_rois.y = src_roi_y;
  d_ptr_->src_rois.w = src_roi_w;
  d_ptr_->src_rois.h = src_roi_h;
  d_ptr_->dst_rois.x = dst_roi_x;
  d_ptr_->dst_rois.y = dst_roi_y;
  d_ptr_->dst_rois.w = dst_roi_w;
  d_ptr_->dst_rois.h = dst_roi_h;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_rgbx_ptrs_mlu_,
                          reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_cpu_),
                          sizeof(char*) * d_ptr_->batch_size,
                          CNRT_MEM_TRANS_DIR_HOST2DEV),
                          "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_,
                          reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
                          sizeof(char*) * d_ptr_->batch_size,
                          CNRT_MEM_TRANS_DIR_HOST2DEV),
                          "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeRgbx(d_ptr_->handle,
                 d_ptr_->batch_size,
                 d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#endif
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
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
int mluop_resize_rgbx_destroy(HANDLE h) {
  CVResizeRgbxPrivate *d_ptr_ = static_cast<CVResizeRgbxPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop resize rgbx op not init\n");
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
  if (d_ptr_->src_rgbx_ptrs_cpu_) {
    free(d_ptr_->src_rgbx_ptrs_cpu_);
    d_ptr_->src_rgbx_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->src_rgbx_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_rgbx_ptrs_mlu_), "cnrtFree");
    d_ptr_->src_rgbx_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->dst_rgbx_ptrs_cpu_) {
    free(d_ptr_->dst_rgbx_ptrs_cpu_);
    d_ptr_->dst_rgbx_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_rgbx_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->dst_rgbx_ptrs_mlu_), "cnrtFree");
    d_ptr_->dst_rgbx_ptrs_mlu_ = nullptr;
  }
  d_ptr_->dst_rgbx_ptrs_cache_.clear();

  if (d_ptr_->workspace) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->workspace), "cnrtFree");
    d_ptr_->workspace = nullptr;
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

int mluOpResizeRgbxInit(HANDLE *h,
                           int input_w,
                           int input_h,
                           int output_w,
                           int output_h,
                           const char *pix_fmt,
                           const char *depth) {
  CVResizeRgbxPrivate *d_ptr_ = new CVResizeRgbxPrivate;
  MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue_), "mluQueueCreate");
  MLUOP_CV_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_CV_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue_), "cncvSetQueue");

  d_ptr_->batch_size = 1;
  d_ptr_->input_h = input_h;
  d_ptr_->input_w = PAD_UP(input_w, ALIGN_R_SCALE);
  d_ptr_->output_h = output_h;
  d_ptr_->output_w = PAD_UP(output_w, ALIGN_R_SCALE);

  d_ptr_->depth_size = getSizeOfDepth(getCNCVDepthFromIndex(depth));
  d_ptr_->pix_chn_num = getPixFmtChannelNum(getCNCVPixFmtFromPixindex(pix_fmt));
  d_ptr_->input_stride_in_bytes = d_ptr_->input_w *
                                  d_ptr_->pix_chn_num * d_ptr_->depth_size;
  d_ptr_->output_stride_in_bytes = d_ptr_->output_w *
                                  d_ptr_->pix_chn_num * d_ptr_->depth_size;
  d_ptr_->src_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) *
                                  d_ptr_->batch_size));
  d_ptr_->dst_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) *
                                  d_ptr_->batch_size));
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_rgbx_ptrs_mlu_),
                                  sizeof(char*) * d_ptr_->batch_size),
                                  "cnrtMalloc");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_rgbx_ptrs_mlu_),
                                  sizeof(char*) * d_ptr_->batch_size),
                                  "cnrtMalloc");

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

  #ifdef DEBUG
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_begin),"mluNotifierCreate");
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_end),  "mluNotifierCreate");
  #endif

#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvGetResizeRgbxAdvancedWorkspaceSize(
                                      d_ptr_->batch_size,
                                      &d_ptr_->src_desc,
                                      &d_ptr_->src_rois,
                                      &d_ptr_->dst_desc,
                                      &d_ptr_->dst_rois,
                                      CNCV_INTER_BILINEAR,
                                      &d_ptr_->workspace_size),
                                      "cncvGetResizeRgbxAdvancedWorkspaceSize");
#else
  MLUOP_CV_CHECK(cncvGetResizeRgbxWorkspaceSize(d_ptr_->batch_size,
                                  &d_ptr_->workspace_size),
                                  "cncvGetResizeRgbxWorkspaceSize");
#endif
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->workspace),
                                  d_ptr_->workspace_size), "cnrtMalloc");

  *h = static_cast<void *>(d_ptr_);

  return 0;
}

int mluRpResizeRgbxExec(HANDLE h, void *input, void *output) {
  CVResizeRgbxPrivate *d_ptr_ = static_cast<CVResizeRgbxPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     printf("Sync queue is null.");
     return -1;
  }
  for (uint32_t bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_rgbx_ptrs_cpu_[bi] = input;
    d_ptr_->dst_rgbx_ptrs_cpu_[bi] = output;
  }
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_rgbx_ptrs_mlu_,
              reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_cpu_),
              sizeof(char*) * d_ptr_->batch_size,
              CNRT_MEM_TRANS_DIR_HOST2DEV),
              "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_,
              reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
              sizeof(char*) * d_ptr_->batch_size,
              CNRT_MEM_TRANS_DIR_HOST2DEV),
              "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeRgbx(d_ptr_->handle,
                 d_ptr_->batch_size,
                 d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#endif
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
                "cnrtPlaceNorifier");
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

int mluOpResizeRgbxExecPad(HANDLE h,
                               void *input, void *output) {
  CVResizeRgbxPrivate *d_ptr_ = static_cast<CVResizeRgbxPrivate *>(h);
  const float EPSINON = 0.00001f;

  if (nullptr == d_ptr_->queue_) {
     printf("Sync queue is null.");
     return -1;
  }

  for (uint32_t bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_rgbx_ptrs_cpu_[bi] = input;
    d_ptr_->dst_rgbx_ptrs_cpu_[bi] = output;
  }

  cnrtRet_t cnret;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_rgbx_ptrs_mlu_,
                          reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_cpu_),
                          sizeof(char*) * d_ptr_->batch_size,
                          CNRT_MEM_TRANS_DIR_HOST2DEV),
                          "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_,
                          reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
                          sizeof(char*) * d_ptr_->batch_size,
                          CNRT_MEM_TRANS_DIR_HOST2DEV),
                          "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemset(output, 0,
                          d_ptr_->dst_desc.stride[0] * d_ptr_->dst_desc.height),
                          "cnrtMemset");
  int low_bound_p, low_bound_len;
  float src_scale = (float) d_ptr_->src_desc.width / d_ptr_->src_desc.height;
  float dst_scale = (float) d_ptr_->dst_desc.width / d_ptr_->dst_desc.height;
  float dis = fabs(src_scale - dst_scale);
  if (dis > EPSINON) {
    if(src_scale < dst_scale) {
      d_ptr_->dst_rois.y = 0;
      d_ptr_->dst_rois.h = d_ptr_->dst_desc.height;
      low_bound_len = (d_ptr_->dst_desc.height * d_ptr_->src_desc.width / d_ptr_->src_desc.height) / 2;
      d_ptr_->dst_rois.w = low_bound_len * 2;
      low_bound_p = (d_ptr_->dst_desc.width - d_ptr_->dst_rois.w) / 4;
      d_ptr_->dst_rois.x = low_bound_p * 2;
    } else {
      d_ptr_->dst_rois.x = 0;
      d_ptr_->dst_rois.w = d_ptr_->dst_desc.width;
      low_bound_len = (d_ptr_->dst_desc.width * d_ptr_->src_desc.height / d_ptr_->src_desc.width) / 2;
      d_ptr_->dst_rois.h = low_bound_len * 2;
      low_bound_p = (d_ptr_->dst_desc.height - d_ptr_->dst_rois.h) / 4;
      d_ptr_->dst_rois.y = low_bound_p * 2;
    }
  }
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeRgbx(d_ptr_->handle,
                 d_ptr_->batch_size,
                 d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#endif
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
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

int mluOpResizeRgbxExecRoi(HANDLE h,
                            void *input, void *output,
                            uint32_t src_roi_x, uint32_t src_roi_y,
                            uint32_t src_roi_w, uint32_t src_roi_h,
                            uint32_t dst_roi_x, uint32_t dst_roi_y,
                            uint32_t dst_roi_w, uint32_t dst_roi_h) {
  CVResizeRgbxPrivate *d_ptr_ = static_cast<CVResizeRgbxPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
    printf("Sync queue is null.\n");
    return -1;
  }
  if (src_roi_x%2 || src_roi_y%2 || src_roi_w%2 || src_roi_h%2 ||
      dst_roi_x%2 || dst_roi_y%2 || dst_roi_w%2 || dst_roi_h%2) {
    printf("roi params illegal, value must be even.\n");
    return -1;
  }
  if ((src_roi_x + src_roi_w > d_ptr_->src_desc.width)  ||
      (src_roi_y + src_roi_h > d_ptr_->src_desc.height) ||
      (dst_roi_x + dst_roi_w > d_ptr_->dst_desc.width ) ||
      (dst_roi_y + dst_roi_h > d_ptr_->dst_desc.height)) {
    printf("roi params illegal, the ROI area must be within the image scope.\n");
    return -1;
  }
  for (uint32_t bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_rgbx_ptrs_cpu_[bi] = input;
    d_ptr_->dst_rgbx_ptrs_cpu_[bi] = output;
  }
  d_ptr_->src_rois.x = src_roi_x;
  d_ptr_->src_rois.y = src_roi_y;
  d_ptr_->src_rois.w = src_roi_w;
  d_ptr_->src_rois.h = src_roi_h;
  d_ptr_->dst_rois.x = dst_roi_x;
  d_ptr_->dst_rois.y = dst_roi_y;
  d_ptr_->dst_rois.w = dst_roi_w;
  d_ptr_->dst_rois.h = dst_roi_h;
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_rgbx_ptrs_mlu_,
                          reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_cpu_),
                          sizeof(char*) * d_ptr_->batch_size,
                          CNRT_MEM_TRANS_DIR_HOST2DEV),
                          "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_,
                          reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
                          sizeof(char*) * d_ptr_->batch_size,
                          CNRT_MEM_TRANS_DIR_HOST2DEV),
                          "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeRgbx(d_ptr_->handle,
                 d_ptr_->batch_size,
                 d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeRgbx_AdvancedROI(d_ptr_->handle,
                 d_ptr_->batch_size,
                 &d_ptr_->src_desc,
                 &d_ptr_->src_rois,
                 reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_mlu_),
                 &d_ptr_->dst_desc,
                 &d_ptr_->dst_rois,
                 reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                 d_ptr_->workspace_size,
                 d_ptr_->workspace,
                 CNCV_INTER_BILINEAR),
                 "cncvResizeRgbx_AdvancedROI");
#endif
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
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

int mluOpResizeRgbxDestroy(HANDLE h) {
  CVResizeRgbxPrivate *d_ptr_ = static_cast<CVResizeRgbxPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop resize rgbx op not init\n");
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
  if (d_ptr_->src_rgbx_ptrs_cpu_) {
    free(d_ptr_->src_rgbx_ptrs_cpu_);
    d_ptr_->src_rgbx_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->src_rgbx_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_rgbx_ptrs_mlu_), "cnrtFree");
    d_ptr_->src_rgbx_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->dst_rgbx_ptrs_cpu_) {
    free(d_ptr_->dst_rgbx_ptrs_cpu_);
    d_ptr_->dst_rgbx_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_rgbx_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->dst_rgbx_ptrs_mlu_), "cnrtFree");
    d_ptr_->dst_rgbx_ptrs_mlu_ = nullptr;
  }
  d_ptr_->dst_rgbx_ptrs_cache_.clear();

  if (d_ptr_->workspace) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->workspace), "cnrtFree");
    d_ptr_->workspace = nullptr;
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
