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
extern cncvStatus_t cncvResizeYuv(cncvHandle_t handle,
                                  const uint32_t batch_size,
                                  const cncvImageDescriptor *src_desc,
                                  const cncvRect *src_rois,
                                  void **src,
                                  const cncvImageDescriptor *dst_desc,
                                  void **dst,
                                  const cncvRect *dst_rois,
                                  const size_t workspace_size,
                                  void *workspace,
                                  cncvInterpolation interpolation);
#else
#if CNCV_MAJOR >= 2
extern cncvStatus_t cncvResizeYuv_AdvancedROI(cncvHandle_t handle,
                                const uint32_t batch_size,
                                const cncvImageDescriptor *src_desc,
                                const cncvRect *src_rois,
                                cncvBufferList_t src,
                                const cncvImageDescriptor *dst_desc,
                                const cncvRect *dst_rois,
                                cncvBufferList_t dst,
                                const size_t workspace_size,
                                void *workspace,
                                cncvInterpolation interpolation);
#else
extern cncvStatus_t cncvResizeYuv_AdvancedROI(cncvHandle_t handle,
                                const uint32_t batch_size,
                                const cncvImageDescriptor *src_desc,
                                const cncvRect *src_rois,
                                void **src,
                                const cncvImageDescriptor *dst_desc,
                                const cncvRect *dst_rois,
                                void **dst,
                                const size_t workspace_size,
                                void *workspace,
                                cncvInterpolation interpolation);
#endif
#endif
extern cncvStatus_t cncvGetResizeYuvWorkspaceSize(const uint32_t batch_size,
                                                  const cncvImageDescriptor *src_desc,
                                                  const cncvRect *src_rois,
                                                  const cncvImageDescriptor *dst_desc,
                                                  const cncvRect *dst_rois,
                                                  size_t *workspace_size);

int mluOpGetVersion (void) {
  int op_ver = MLUOP_VERSION;
  return op_ver;
}

struct CVResizeYUVPrivate {
public:
  int batch_size = 1;
  int input_w, input_h, output_w, output_h;

  cnrtQueue_t queue_ = nullptr;
  cncvHandle_t handle;
  cncvImageDescriptor *src_desc;
  cncvImageDescriptor *dst_desc;
  cncvRect *src_rois;
  cncvRect *dst_rois;

  size_t workspace_size = 0;
  void *workspace = nullptr;

  float sw_time = 0.0;
  float hw_time = 0.0;
  struct timeval end;
  struct timeval start;
  cnrtNotifier_t event_begin = nullptr;
  cnrtNotifier_t event_end   = nullptr;

  void **src_ptrs_cpu_ = nullptr;
  void **src_ptrs_mlu_ = nullptr;
  void **dst_ptrs_cpu_ = nullptr;
  void **dst_ptrs_mlu_ = nullptr;
};  // CVResziePrivate

/**
* will be deprecated from next release version
*/
int mluop_resize_yuv_init(HANDLE *h,
                          int input_w, int input_h,
                          int output_w, int output_h,
                          const char *depth, const char *pix_fmt) {
  CVResizeYUVPrivate *d_ptr_ = new CVResizeYUVPrivate;
  MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue_), "cnrtCreate");
  MLUOP_CV_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_CV_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue_), "cncvSetQueue");

  d_ptr_->input_w = PAD_UP(input_w, ALIGN_Y_SCALE);
  d_ptr_->output_w = PAD_UP(output_w, ALIGN_Y_SCALE);
  d_ptr_->input_h = input_h;
  d_ptr_->output_h = output_h;
  d_ptr_->src_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size * 2));
  d_ptr_->dst_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size * 2));

  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_ptrs_mlu_),
                            sizeof(char*) * d_ptr_->batch_size * 2), "cnrtMalloc");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_ptrs_mlu_),
                            sizeof(char*) * d_ptr_->batch_size * 2), "cnrtMalloc");

  d_ptr_->src_desc = new cncvImageDescriptor[d_ptr_->batch_size];
  for (int i = 0; i < d_ptr_->batch_size; ++i) {
    d_ptr_->src_desc[i].width = d_ptr_->input_w;
    d_ptr_->src_desc[i].height = d_ptr_->input_h;
    d_ptr_->src_desc[i].pixel_fmt = getCNCVPixFmtFromPixindex(pix_fmt);
    d_ptr_->src_desc[i].stride[0] = d_ptr_->input_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
    d_ptr_->src_desc[i].stride[1] = d_ptr_->input_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
    d_ptr_->src_desc[i].depth = getCNCVDepthFromIndex(depth);
  }

  d_ptr_->dst_desc = new cncvImageDescriptor[d_ptr_->batch_size];
  for (int i = 0; i < d_ptr_->batch_size; ++i) {
    d_ptr_->dst_desc[i].width = d_ptr_->output_w;
    d_ptr_->dst_desc[i].height = d_ptr_->output_h;
    d_ptr_->dst_desc[i].pixel_fmt = getCNCVPixFmtFromPixindex(pix_fmt);
    d_ptr_->dst_desc[i].stride[0] = d_ptr_->output_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
    d_ptr_->dst_desc[i].stride[1] = d_ptr_->output_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
    d_ptr_->dst_desc[i].depth = getCNCVDepthFromIndex(depth);
  }

  d_ptr_->src_rois = new cncvRect[d_ptr_->batch_size];
  d_ptr_->dst_rois = new cncvRect[d_ptr_->batch_size];
  for (int i = 0; i < d_ptr_->batch_size; i++) {
    d_ptr_->src_rois[i].h = d_ptr_->input_h;
    d_ptr_->src_rois[i].w = d_ptr_->input_w;
    d_ptr_->src_rois[i].x = 0;
    d_ptr_->src_rois[i].y = 0;

    d_ptr_->dst_rois[i].h = d_ptr_->output_h;
    d_ptr_->dst_rois[i].w = d_ptr_->output_w;
    d_ptr_->dst_rois[i].x = 0;
    d_ptr_->dst_rois[i].y = 0;
  }

  MLUOP_CV_CHECK(cncvGetResizeYuvWorkspaceSize(d_ptr_->batch_size,
                                        d_ptr_->src_desc,
                                        d_ptr_->src_rois,
                                        d_ptr_->dst_desc,
                                        d_ptr_->dst_rois,
                                        &d_ptr_->workspace_size),
                                        "cncvGetResizeYuvWorkspaceSize");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->workspace),
                d_ptr_->workspace_size), "cnrtMalloc");
  #ifdef DEBUG
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_begin),"mluNotifierCreate");
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_end),  "mluNotifierCreate");
  #endif

  *h = static_cast<void *>(d_ptr_);
  return 0;
}

int mluOpResizeYuvInit(HANDLE *h,
                          int input_w, int input_h,
                          int output_w, int output_h,
                          const char *depth, const char *pix_fmt) {
  CVResizeYUVPrivate *d_ptr_ = new CVResizeYUVPrivate;
  MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue_), "cnrtCreate");
  MLUOP_CV_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_CV_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue_), "cncvSetQueue");

  d_ptr_->input_w = PAD_UP(input_w, ALIGN_Y_SCALE);
  d_ptr_->output_w = PAD_UP(output_w, ALIGN_Y_SCALE);
  d_ptr_->input_h = input_h;
  d_ptr_->output_h = output_h;
  d_ptr_->src_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size * 2));
  d_ptr_->dst_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size * 2));

  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_ptrs_mlu_),
                            sizeof(char*) * d_ptr_->batch_size * 2), "cnrtMalloc");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_ptrs_mlu_),
                            sizeof(char*) * d_ptr_->batch_size * 2), "cnrtMalloc");

  d_ptr_->src_desc = new cncvImageDescriptor[d_ptr_->batch_size];
  for (int i = 0; i < d_ptr_->batch_size; ++i) {
    d_ptr_->src_desc[i].width = d_ptr_->input_w;
    d_ptr_->src_desc[i].height = d_ptr_->input_h;
    d_ptr_->src_desc[i].pixel_fmt = getCNCVPixFmtFromPixindex(pix_fmt);
    d_ptr_->src_desc[i].stride[0] = d_ptr_->input_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
    d_ptr_->src_desc[i].stride[1] = d_ptr_->input_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
    d_ptr_->src_desc[i].depth = getCNCVDepthFromIndex(depth);
  }

  d_ptr_->dst_desc = new cncvImageDescriptor[d_ptr_->batch_size];
  for (int i = 0; i < d_ptr_->batch_size; ++i) {
    d_ptr_->dst_desc[i].width = d_ptr_->output_w;
    d_ptr_->dst_desc[i].height = d_ptr_->output_h;
    d_ptr_->dst_desc[i].pixel_fmt = getCNCVPixFmtFromPixindex(pix_fmt);
    d_ptr_->dst_desc[i].stride[0] = d_ptr_->output_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
    d_ptr_->dst_desc[i].stride[1] = d_ptr_->output_w * getSizeOfDepth(getCNCVDepthFromIndex(depth));
    d_ptr_->dst_desc[i].depth = getCNCVDepthFromIndex(depth);
  }

  d_ptr_->src_rois = new cncvRect[d_ptr_->batch_size];
  d_ptr_->dst_rois = new cncvRect[d_ptr_->batch_size];
  for (int i = 0; i < d_ptr_->batch_size; i++) {
    d_ptr_->src_rois[i].h = d_ptr_->input_h;
    d_ptr_->src_rois[i].w = d_ptr_->input_w;
    d_ptr_->src_rois[i].x = 0;
    d_ptr_->src_rois[i].y = 0;

    d_ptr_->dst_rois[i].h = d_ptr_->output_h;
    d_ptr_->dst_rois[i].w = d_ptr_->output_w;
    d_ptr_->dst_rois[i].x = 0;
    d_ptr_->dst_rois[i].y = 0;
  }

  MLUOP_CV_CHECK(cncvGetResizeYuvWorkspaceSize(d_ptr_->batch_size,
                                        d_ptr_->src_desc,
                                        d_ptr_->src_rois,
                                        d_ptr_->dst_desc,
                                        d_ptr_->dst_rois,
                                        &d_ptr_->workspace_size),
                                        "cncvGetResizeYuvWorkspaceSize");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->workspace),
                d_ptr_->workspace_size), "cnrtMalloc");
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
int mluop_resize_yuv_exec(HANDLE h,
                          void *input_y, void *input_uv,
                          void *output_y, void *output_uv) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     printf("Sync queue is null.");
     return -1;
  }
  for (int bi = 0; bi < d_ptr_->batch_size; bi++) {
    d_ptr_->src_ptrs_cpu_[bi * 2]     = input_y;
    d_ptr_->src_ptrs_cpu_[bi * 2 + 1] = input_uv;
    d_ptr_->dst_ptrs_cpu_[bi * 2]     = output_y;
    d_ptr_->dst_ptrs_cpu_[bi * 2 + 1] = output_uv;
  }
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu_,
                            reinterpret_cast<void**>(d_ptr_->src_ptrs_cpu_),
                            sizeof(char*) * d_ptr_->batch_size * 2,
                            CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu_,
                            reinterpret_cast<void**>(d_ptr_->dst_ptrs_cpu_),
                            sizeof(char*) * d_ptr_->batch_size * 2,
                            CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeYuv(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        d_ptr_->src_ptrs_mlu_,
                        d_ptr_->dst_desc,
                        d_ptr_->dst_ptrs_mlu_,
                        d_ptr_->dst_rois,
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<void**>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<void**>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
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

int mluOpResizeYuvExec(HANDLE h,
                          void *input_y, void *input_uv,
                          void *output_y, void *output_uv) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     printf("Sync queue is null.");
     return -1;
  }
  for (int bi = 0; bi < d_ptr_->batch_size; bi++) {
    d_ptr_->src_ptrs_cpu_[bi * 2]     = input_y;
    d_ptr_->src_ptrs_cpu_[bi * 2 + 1] = input_uv;
    d_ptr_->dst_ptrs_cpu_[bi * 2]     = output_y;
    d_ptr_->dst_ptrs_cpu_[bi * 2 + 1] = output_uv;
  }
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu_,
                            reinterpret_cast<void**>(d_ptr_->src_ptrs_cpu_),
                            sizeof(char*) * d_ptr_->batch_size * 2,
                            CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu_,
                            reinterpret_cast<void**>(d_ptr_->dst_ptrs_cpu_),
                            sizeof(char*) * d_ptr_->batch_size * 2,
                            CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeYuv(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        d_ptr_->src_ptrs_mlu_,
                        d_ptr_->dst_desc,
                        d_ptr_->dst_ptrs_mlu_,
                        d_ptr_->dst_rois,
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<void**>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<void**>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
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
int mluop_resize_pad_yuv_exec(HANDLE h,
                              void *input_y, void *input_uv,
                              void *output_y, void *output_uv) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     printf("Sync queue is null.");
     return -1;
  }
  const float EPSINON = 0.00001f;

  for (int bi = 0; bi < d_ptr_->batch_size; bi++) {
    d_ptr_->src_ptrs_cpu_[bi * 2]     = input_y;
    d_ptr_->src_ptrs_cpu_[bi * 2 + 1] = input_uv;
    d_ptr_->dst_ptrs_cpu_[bi * 2]     = output_y;
    d_ptr_->dst_ptrs_cpu_[bi * 2 + 1] = output_uv;
  }
  MLUOP_RT_CHECK(cnrtMemset(output_y, 0,
                d_ptr_->dst_desc[0].width * d_ptr_->dst_desc[0].height),
                "cnrtMemset");
  MLUOP_RT_CHECK(cnrtMemset(output_uv, 128,
                d_ptr_->dst_desc[0].width*d_ptr_->dst_desc[0].height/2),
                "cnrtMemset");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu_,
                reinterpret_cast<void**>(d_ptr_->src_ptrs_cpu_),
                sizeof(char*) * d_ptr_->batch_size * 2,
                CNRT_MEM_TRANS_DIR_HOST2DEV),
                "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu_,
                reinterpret_cast<void**>(d_ptr_->dst_ptrs_cpu_),
                sizeof(char*) * d_ptr_->batch_size * 2,
                CNRT_MEM_TRANS_DIR_HOST2DEV),
                "cnrtMemcpy");

  int low_bound_p, low_bound_len;
  float src_scale = (float) d_ptr_->src_desc[0].width / d_ptr_->src_desc[0].height;
  float dst_scale = (float) d_ptr_->dst_desc[0].width / d_ptr_->dst_desc[0].height;
  float dis = fabs(src_scale - dst_scale);
  if (dis > EPSINON) {
    if(src_scale < dst_scale) {
      d_ptr_->dst_rois[0].y = 0;
      d_ptr_->dst_rois[0].h = d_ptr_->dst_desc[0].height;
      low_bound_len = (d_ptr_->dst_desc[0].height * d_ptr_->src_desc[0].width / d_ptr_->src_desc[0].height) / 2;
      d_ptr_->dst_rois[0].w = low_bound_len * 2;
      low_bound_p = (d_ptr_->dst_desc[0].width - d_ptr_->dst_rois[0].w) / 4;
      d_ptr_->dst_rois[0].x = low_bound_p * 2;
    } else {
      d_ptr_->dst_rois[0].x = 0;
      d_ptr_->dst_rois[0].w = d_ptr_->dst_desc[0].width;
      low_bound_len = (d_ptr_->dst_desc[0].width * d_ptr_->src_desc[0].height / d_ptr_->src_desc[0].width) / 2;
      d_ptr_->dst_rois[0].h = low_bound_len * 2;
      low_bound_p = (d_ptr_->dst_desc[0].height - d_ptr_->dst_rois[0].h) / 4;
      d_ptr_->dst_rois[0].y = low_bound_p * 2;
    }
  }
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeYuv(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        d_ptr_->src_ptrs_mlu_,
                        d_ptr_->dst_desc,
                        d_ptr_->dst_ptrs_mlu_,
                        d_ptr_->dst_rois,
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<void**>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<void**>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
#endif
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
  MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  MLUOP_RT_CHECK(cnrtNotifierDuration(d_ptr_->event_begin,
                d_ptr_->event_end, &d_ptr_->hw_time), "cnrtNotifierDuration");
  d_ptr_->sw_time = (d_ptr_->end.tv_sec - d_ptr_->start.tv_sec) * 1000000
                    + (d_ptr_->end.tv_usec - d_ptr_->start.tv_usec);
  printf("hw time: %.3f ms, sw time: %.3f ms\n",
        d_ptr_->hw_time/1000.f, d_ptr_->sw_time/1000.f);
  #endif

  return 0;
}

int mluOpResizeYuvExecPad(HANDLE h,
                              void *input_y, void *input_uv,
                              void *output_y, void *output_uv) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     printf("Sync queue is null.");
     return -1;
  }
  const float EPSINON = 0.00001f;

  for (int bi = 0; bi < d_ptr_->batch_size; bi++) {
    d_ptr_->src_ptrs_cpu_[bi * 2]     = input_y;
    d_ptr_->src_ptrs_cpu_[bi * 2 + 1] = input_uv;
    d_ptr_->dst_ptrs_cpu_[bi * 2]     = output_y;
    d_ptr_->dst_ptrs_cpu_[bi * 2 + 1] = output_uv;
  }
  MLUOP_RT_CHECK(cnrtMemset(output_y, 0,
                d_ptr_->dst_desc[0].width * d_ptr_->dst_desc[0].height),
                "cnrtMemset");
  MLUOP_RT_CHECK(cnrtMemset(output_uv, 128,
                d_ptr_->dst_desc[0].width*d_ptr_->dst_desc[0].height/2),
                "cnrtMemset");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu_,
                reinterpret_cast<void**>(d_ptr_->src_ptrs_cpu_),
                sizeof(char*) * d_ptr_->batch_size * 2,
                CNRT_MEM_TRANS_DIR_HOST2DEV),
                "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu_,
                reinterpret_cast<void**>(d_ptr_->dst_ptrs_cpu_),
                sizeof(char*) * d_ptr_->batch_size * 2,
                CNRT_MEM_TRANS_DIR_HOST2DEV),
                "cnrtMemcpy");

  int low_bound_p, low_bound_len;
  float src_scale = (float) d_ptr_->src_desc[0].width / d_ptr_->src_desc[0].height;
  float dst_scale = (float) d_ptr_->dst_desc[0].width / d_ptr_->dst_desc[0].height;
  float dis = fabs(src_scale - dst_scale);
  if (dis > EPSINON) {
    if(src_scale < dst_scale) {
      d_ptr_->dst_rois[0].y = 0;
      d_ptr_->dst_rois[0].h = d_ptr_->dst_desc[0].height;
      low_bound_len = (d_ptr_->dst_desc[0].height * d_ptr_->src_desc[0].width / d_ptr_->src_desc[0].height) / 2;
      d_ptr_->dst_rois[0].w = low_bound_len * 2;
      low_bound_p = (d_ptr_->dst_desc[0].width - d_ptr_->dst_rois[0].w) / 4;
      d_ptr_->dst_rois[0].x = low_bound_p * 2;
    } else {
      d_ptr_->dst_rois[0].x = 0;
      d_ptr_->dst_rois[0].w = d_ptr_->dst_desc[0].width;
      low_bound_len = (d_ptr_->dst_desc[0].width * d_ptr_->src_desc[0].height / d_ptr_->src_desc[0].width) / 2;
      d_ptr_->dst_rois[0].h = low_bound_len * 2;
      low_bound_p = (d_ptr_->dst_desc[0].height - d_ptr_->dst_rois[0].h) / 4;
      d_ptr_->dst_rois[0].y = low_bound_p * 2;
    }
  }
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeYuv(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        d_ptr_->src_ptrs_mlu_,
                        d_ptr_->dst_desc,
                        d_ptr_->dst_ptrs_mlu_,
                        d_ptr_->dst_rois,
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<void**>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<void**>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
#endif
#endif
  #ifdef DEBUG
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
  MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->end, NULL);
  MLUOP_RT_CHECK(cnrtNotifierDuration(d_ptr_->event_begin,
                d_ptr_->event_end, &d_ptr_->hw_time), "cnrtNotifierDuration");
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
int mluop_resize_roi_yuv_exec(HANDLE h,
                              void *input_y, void *input_uv,
                              void *output_y, void *output_uv,
                              uint32_t src_roi_x, uint32_t src_roi_y,
                              uint32_t src_roi_w, uint32_t src_roi_h,
                              uint32_t dst_roi_x, uint32_t dst_roi_y,
                              uint32_t dst_roi_w, uint32_t dst_roi_h) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
    printf("Sync queue is null.\n");
    return -1;
  }
  if (src_roi_x%2 || src_roi_y%2 ||
      src_roi_w%2 || src_roi_h%2 ||
      dst_roi_x%2 || dst_roi_y%2 ||
      dst_roi_w%2 || dst_roi_h%2) {
    printf("roi params illegal, value must be even.\n");
    return -1;
  }
  if ((src_roi_x + src_roi_w > d_ptr_->src_desc->width)  ||
      (src_roi_y + src_roi_h > d_ptr_->src_desc->height) ||
      (dst_roi_x + dst_roi_w > d_ptr_->dst_desc->width ) ||
      (dst_roi_y + dst_roi_h > d_ptr_->dst_desc->height)) {
    printf("roi value illegal, the ROI area must be within the image scope.\n");
    return -1;
  }
  for (int ri = 0; ri < d_ptr_->batch_size; ++ri) {
    d_ptr_->src_rois[ri].x = src_roi_x;
    d_ptr_->src_rois[ri].y = src_roi_y;
    d_ptr_->src_rois[ri].w = src_roi_w;
    d_ptr_->src_rois[ri].h = src_roi_h;
    d_ptr_->dst_rois[ri].x = dst_roi_x;
    d_ptr_->dst_rois[ri].y = dst_roi_y;
    d_ptr_->dst_rois[ri].w = dst_roi_w;
    d_ptr_->dst_rois[ri].h = dst_roi_h;
  }
  for (int bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_ptrs_cpu_[bi * 2]     = input_y;
    d_ptr_->src_ptrs_cpu_[bi * 2 + 1] = input_uv;
    d_ptr_->dst_ptrs_cpu_[bi * 2]     = output_y;
    d_ptr_->dst_ptrs_cpu_[bi * 2 + 1] = output_uv;
  }
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu_,
                  reinterpret_cast<void**>(d_ptr_->src_ptrs_cpu_),
                  sizeof(char*) * d_ptr_->batch_size * 2,
                  CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu_,
                  reinterpret_cast<void**>(d_ptr_->dst_ptrs_cpu_),
                  sizeof(char*) * d_ptr_->batch_size * 2,
                  CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeYuv(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        d_ptr_->src_ptrs_mlu_,
                        d_ptr_->dst_desc,
                        d_ptr_->dst_ptrs_mlu_,
                        d_ptr_->dst_rois,
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<void**>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<void**>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
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

int mluOpResizeYuvExecRoi(HANDLE h,
                              void *input_y, void *input_uv,
                              void *output_y, void *output_uv,
                              uint32_t src_roi_x, uint32_t src_roi_y,
                              uint32_t src_roi_w, uint32_t src_roi_h,
                              uint32_t dst_roi_x, uint32_t dst_roi_y,
                              uint32_t dst_roi_w, uint32_t dst_roi_h) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
    printf("Sync queue is null.\n");
    return -1;
  }
  if (src_roi_x%2 || src_roi_y%2 ||
      src_roi_w%2 || src_roi_h%2 ||
      dst_roi_x%2 || dst_roi_y%2 ||
      dst_roi_w%2 || dst_roi_h%2) {
    printf("roi params illegal, value must be even.\n");
    return -1;
  }
  if ((src_roi_x + src_roi_w > d_ptr_->src_desc->width)  ||
      (src_roi_y + src_roi_h > d_ptr_->src_desc->height) ||
      (dst_roi_x + dst_roi_w > d_ptr_->dst_desc->width ) ||
      (dst_roi_y + dst_roi_h > d_ptr_->dst_desc->height)) {
    printf("roi value illegal, the ROI area must be within the image scope.\n");
    return -1;
  }
  for (int ri = 0; ri < d_ptr_->batch_size; ++ri) {
    d_ptr_->src_rois[ri].x = src_roi_x;
    d_ptr_->src_rois[ri].y = src_roi_y;
    d_ptr_->src_rois[ri].w = src_roi_w;
    d_ptr_->src_rois[ri].h = src_roi_h;
    d_ptr_->dst_rois[ri].x = dst_roi_x;
    d_ptr_->dst_rois[ri].y = dst_roi_y;
    d_ptr_->dst_rois[ri].w = dst_roi_w;
    d_ptr_->dst_rois[ri].h = dst_roi_h;
  }
  for (int bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_ptrs_cpu_[bi * 2]     = input_y;
    d_ptr_->src_ptrs_cpu_[bi * 2 + 1] = input_uv;
    d_ptr_->dst_ptrs_cpu_[bi * 2]     = output_y;
    d_ptr_->dst_ptrs_cpu_[bi * 2 + 1] = output_uv;
  }
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_ptrs_mlu_,
                  reinterpret_cast<void**>(d_ptr_->src_ptrs_cpu_),
                  sizeof(char*) * d_ptr_->batch_size * 2,
                  CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_ptrs_mlu_,
                  reinterpret_cast<void**>(d_ptr_->dst_ptrs_cpu_),
                  sizeof(char*) * d_ptr_->batch_size * 2,
                  CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif

#if (CNCV_MAJOR == 0 && CNCV_MINOR < 8) || CNCV_PATCHLEVEL > 100
  MLUOP_CV_CHECK(cncvResizeYuv(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        d_ptr_->src_ptrs_mlu_,
                        d_ptr_->dst_desc,
                        d_ptr_->dst_ptrs_mlu_,
                        d_ptr_->dst_rois,
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv");
#else
#if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
#else
  MLUOP_CV_CHECK(cncvResizeYuv_AdvancedROI(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        reinterpret_cast<void**>(d_ptr_->src_ptrs_mlu_),
                        d_ptr_->dst_desc,
                        d_ptr_->dst_rois,
                        reinterpret_cast<void**>(d_ptr_->dst_ptrs_mlu_),
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR), "cncvResizeYuv_AdvancedROI");
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
int mluop_resize_yuv_destroy(HANDLE h) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop resize yuv op not init\n");
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
  if (d_ptr_->src_ptrs_cpu_) {
    free(d_ptr_->src_ptrs_cpu_);
    d_ptr_->src_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->src_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_ptrs_mlu_), "cnrtFree");
    d_ptr_->src_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->dst_ptrs_cpu_) {
    free(d_ptr_->dst_ptrs_cpu_);
    d_ptr_->dst_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->dst_ptrs_mlu_), "cnrtFree");
    d_ptr_->dst_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->workspace) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->workspace), "cnrtFree");
    d_ptr_->workspace = nullptr;
  }
  if (d_ptr_->src_desc) {
    delete[] d_ptr_->src_desc;
    d_ptr_->src_desc = nullptr;
  }
  if (d_ptr_->dst_desc) {
    delete[] d_ptr_->dst_desc;
    d_ptr_->dst_desc = nullptr;
  }
  if (d_ptr_->src_rois) {
    delete[] d_ptr_->src_rois;
    d_ptr_->src_rois = nullptr;
  }
  if (d_ptr_->dst_rois) {
    delete[] d_ptr_->dst_rois;
    d_ptr_->dst_rois = nullptr;
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

int mluOpResizeYuvDestroy(HANDLE h) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop resize yuv op not init\n");
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
  if (d_ptr_->src_ptrs_cpu_) {
    free(d_ptr_->src_ptrs_cpu_);
    d_ptr_->src_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->src_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_ptrs_mlu_), "cnrtFree");
    d_ptr_->src_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->dst_ptrs_cpu_) {
    free(d_ptr_->dst_ptrs_cpu_);
    d_ptr_->dst_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->dst_ptrs_mlu_), "cnrtFree");
    d_ptr_->dst_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->workspace) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->workspace), "cnrtFree");
    d_ptr_->workspace = nullptr;
  }
  if (d_ptr_->src_desc) {
    delete[] d_ptr_->src_desc;
    d_ptr_->src_desc = nullptr;
  }
  if (d_ptr_->dst_desc) {
    delete[] d_ptr_->dst_desc;
    d_ptr_->dst_desc = nullptr;
  }
  if (d_ptr_->src_rois) {
    delete[] d_ptr_->src_rois;
    d_ptr_->src_rois = nullptr;
  }
  if (d_ptr_->dst_rois) {
    delete[] d_ptr_->dst_rois;
    d_ptr_->dst_rois = nullptr;
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
