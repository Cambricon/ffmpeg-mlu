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

#if CNCV_MAJOR >= 2
extern cncvStatus_t cncvYuvToRgbx(cncvHandle_t handle,
                                      const uint32_t batch_size,
                                      const cncvImageDescriptor src_desc,
                                      cncvBufferList_t src,
                                      const cncvImageDescriptor dst_desc,
                                      cncvBufferList_t dst,
                                      const size_t workspace_size,
                                      void *workspace);
#else
extern cncvStatus_t cncvYuvToRgbx(cncvHandle_t handle,
                                      const uint32_t batch_size,
                                      const cncvImageDescriptor src_desc,
                                      void **src,
                                      const cncvImageDescriptor dst_desc,
                                      void **dst,
                                      const size_t workspace_size,
                                      void *workspace);
#endif
extern cncvStatus_t cncvGetYuvToRgbxWorkspaceSize(cncvPixelFormat src_pixfmt,
                                                  cncvPixelFormat dst_pixfmt,
                                                  size_t *size);

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

  float sw_time = 0.0;
  float hw_time = 0.0;
  struct timeval end;
  struct timeval start;
  cnrtNotifier_t event_begin = nullptr;
  cnrtNotifier_t event_end   = nullptr;

  std::deque<std::pair<void*, void*>> src_yuv_ptrs_cache_;
  void **src_yuv_ptrs_cpu_;
  void **src_yuv_ptrs_mlu_;
  std::deque<void*> dst_rgbx_ptrs_cache_;
  void **dst_rgbx_ptrs_cpu_;
  void **dst_rgbx_ptrs_mlu_;

};  // CVResziePrivate

/**
* will be deprecated from next release version
*/
int mluop_convert_yuv2rgbx_init(HANDLE *h, int width, int height, const char *src_pix_fmt,
                                const char *dst_pix_fmt, const char *depth) {
  CVConvertYUV2RGBXPrivate *d_ptr_ = new CVConvertYUV2RGBXPrivate;
  MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue_), "mluQueueCreate");
  MLUOP_CV_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_CV_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue_), "cncvSetQueue");

  d_ptr_->width = PAD_UP(width, ALIGN_Y2R_CVT);
  d_ptr_->src_yuv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size * 2));
  d_ptr_->dst_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  cnrtRet_t cnret;
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_yuv_ptrs_mlu_),
                            d_ptr_->batch_size * 2 * sizeof(char*)),
                            "cnrtMalloc");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_rgbx_ptrs_mlu_),
                            d_ptr_->batch_size * sizeof(void*)),
                            "cnrtMalloc");
  MLUOP_CV_CHECK(cncvGetYuvToRgbxWorkspaceSize(
                                    getCNCVPixFmtFromPixindex(src_pix_fmt),
                                    getCNCVPixFmtFromPixindex(dst_pix_fmt),
                                    &d_ptr_->workspace_size),
                                    "cncvGetYuvToRgbxWorkspaceSize");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->workspace),
                            d_ptr_->workspace_size), "cnrtMalloc");
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

  #ifdef DEBUG
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_begin),
                "mluNotifierCreate");
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_end),
                "mluNotifierCreate");
  #endif

  *h = static_cast<void *>(d_ptr_);

  return 0;
}

/**
* will be deprecated from next release version
*/
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
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_yuv_ptrs_mlu_,
                            reinterpret_cast<void**>(d_ptr_->src_yuv_ptrs_cpu_),
                            sizeof(char*) * d_ptr_->batch_size * 2,
                            CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_,
                            reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
                            sizeof(char*) * d_ptr_->batch_size,
                            CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
  #if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvYuvToRgbx(d_ptr_->handle,
                     d_ptr_->batch_size,
                     d_ptr_->src_desc,
                     reinterpret_cast<cncvBufferList_t>(d_ptr_->src_yuv_ptrs_mlu_),
                     d_ptr_->dst_desc,
                     reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_rgbx_ptrs_mlu_),
                     d_ptr_->workspace_size,
                     d_ptr_->workspace), "cncvYuvToRgbx");
  #else
  MLUOP_CV_CHECK(cncvYuvToRgbx(d_ptr_->handle,
                     d_ptr_->batch_size,
                     d_ptr_->src_desc,
                     reinterpret_cast<void**>(d_ptr_->src_yuv_ptrs_mlu_),
                     d_ptr_->dst_desc,
                     reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                     d_ptr_->workspace_size,
                     d_ptr_->workspace), "cncvYuvToRgbx");
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
int mluop_convert_yuv2rgbx_destroy(HANDLE h) {
  CVConvertYUV2RGBXPrivate *d_ptr_ = static_cast<CVConvertYUV2RGBXPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop cvt yuv2rgbx op not init\n");
    return 0;
  }
  #ifdef DEBUG
  if (d_ptr_->event_begin) {
    MLUOP_RT_CHECK(mluNotifierDestroy(d_ptr_->event_begin),
                  "mluNotifierDestroy");
  }
  if (d_ptr_->event_end) {
    MLUOP_RT_CHECK(mluNotifierDestroy(d_ptr_->event_end),
                  "cnrtDestroyNorifier");
  }
  #endif
  if (d_ptr_->src_yuv_ptrs_cpu_) {
    free(d_ptr_->src_yuv_ptrs_cpu_);
    d_ptr_->src_yuv_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->src_yuv_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_yuv_ptrs_mlu_), "cnrtFree");
    d_ptr_->src_yuv_ptrs_mlu_ = nullptr;
  }
  d_ptr_->src_yuv_ptrs_cache_.clear();
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

int mluOpConvertYuv2RgbxInit(HANDLE *h, int width, int height, const char *src_pix_fmt,
                                const char *dst_pix_fmt, const char *depth) {
  CVConvertYUV2RGBXPrivate *d_ptr_ = new CVConvertYUV2RGBXPrivate;
  MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue_), "mluQueueCreate");
  MLUOP_CV_CHECK(cncvCreate(&d_ptr_->handle), "cncvCreate");
  MLUOP_CV_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue_), "cncvSetQueue");

  d_ptr_->width = PAD_UP(width, ALIGN_Y2R_CVT);
  d_ptr_->src_yuv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size * 2));
  d_ptr_->dst_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  cnrtRet_t cnret;
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_yuv_ptrs_mlu_),
                            d_ptr_->batch_size * 2 * sizeof(char*)),
                            "cnrtMalloc");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_rgbx_ptrs_mlu_),
                            d_ptr_->batch_size * sizeof(void*)),
                            "cnrtMalloc");
  MLUOP_CV_CHECK(cncvGetYuvToRgbxWorkspaceSize(
                                    getCNCVPixFmtFromPixindex(src_pix_fmt),
                                    getCNCVPixFmtFromPixindex(dst_pix_fmt),
                                    &d_ptr_->workspace_size),
                                    "cncvGetYuvToRgbxWorkspaceSize");
  MLUOP_RT_CHECK(cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->workspace),
                            d_ptr_->workspace_size), "cnrtMalloc");
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

  #ifdef DEBUG
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_begin),
                "mluNotifierCreate");
  MLUOP_RT_CHECK(mluNotifierCreate(&d_ptr_->event_end),
                "mluNotifierCreate");
  #endif

  *h = static_cast<void *>(d_ptr_);

  return 0;
}

int mluOpConvertYuv2RgbxExec(HANDLE h,
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
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->src_yuv_ptrs_mlu_,
                            reinterpret_cast<void**>(d_ptr_->src_yuv_ptrs_cpu_),
                            sizeof(char*) * d_ptr_->batch_size * 2,
                            CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  MLUOP_RT_CHECK(cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_,
                            reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
                            sizeof(char*) * d_ptr_->batch_size,
                            CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  MLUOP_RT_CHECK(cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_),
                "cnrtPlaceNotifier");
  #endif
  #if CNCV_MAJOR >= 2
  MLUOP_CV_CHECK(cncvYuvToRgbx(d_ptr_->handle,
                     d_ptr_->batch_size,
                     d_ptr_->src_desc,
                     reinterpret_cast<cncvBufferList_t>(d_ptr_->src_yuv_ptrs_mlu_),
                     d_ptr_->dst_desc,
                     reinterpret_cast<cncvBufferList_t>(d_ptr_->dst_rgbx_ptrs_mlu_),
                     d_ptr_->workspace_size,
                     d_ptr_->workspace), "cncvYuvToRgbx");
  #else
  MLUOP_CV_CHECK(cncvYuvToRgbx(d_ptr_->handle,
                     d_ptr_->batch_size,
                     d_ptr_->src_desc,
                     reinterpret_cast<void**>(d_ptr_->src_yuv_ptrs_mlu_),
                     d_ptr_->dst_desc,
                     reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_mlu_),
                     d_ptr_->workspace_size,
                     d_ptr_->workspace), "cncvYuvToRgbx");
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

int mluOpConvertYuv2RgbxDestroy(HANDLE h) {
  CVConvertYUV2RGBXPrivate *d_ptr_ = static_cast<CVConvertYUV2RGBXPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop cvt yuv2rgbx op not init\n");
    return 0;
  }
  #ifdef DEBUG
  if (d_ptr_->event_begin) {
    MLUOP_RT_CHECK(mluNotifierDestroy(d_ptr_->event_begin),
                  "mluNotifierDestroy");
  }
  if (d_ptr_->event_end) {
    MLUOP_RT_CHECK(mluNotifierDestroy(d_ptr_->event_end),
                  "cnrtDestroyNorifier");
  }
  #endif
  if (d_ptr_->src_yuv_ptrs_cpu_) {
    free(d_ptr_->src_yuv_ptrs_cpu_);
    d_ptr_->src_yuv_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->src_yuv_ptrs_mlu_) {
    MLUOP_RT_CHECK(cnrtFree(d_ptr_->src_yuv_ptrs_mlu_), "cnrtFree");
    d_ptr_->src_yuv_ptrs_mlu_ = nullptr;
  }
  d_ptr_->src_yuv_ptrs_cache_.clear();
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
