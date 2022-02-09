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
extern cncvStatus_t cncvGetResizeYuvWorkspaceSize(const uint32_t batch_size,
                                                  const cncvImageDescriptor *src_desc,
                                                  const cncvRect *src_rois,
                                                  const cncvImageDescriptor *dst_desc,
                                                  const cncvRect *dst_rois,
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
  if (strcmp(depth, "8U") == 0 || strcmp(depth, "8u") == 0) {
    return CNCV_DEPTH_8U;
  } else if (strcmp(depth, "16F") == 0 || strcmp(depth, "16f") == 0) {
    return CNCV_DEPTH_16F;
  } else if (strcmp(depth, "32F") == 0 || strcmp(depth, "32f") == 0) {
    return CNCV_DEPTH_32F;
  } else {
    printf("Unsupported depth(%s)\n", depth);
    return CNCV_DEPTH_INVALID;
  }
}

static cncvPixelFormat getCNCVPixFmtFromPixindex(const char* pix_fmt) {
  if (strcmp(pix_fmt, "NV12") == 0 || strcmp(pix_fmt, "nv12") == 0) {
    return CNCV_PIX_FMT_NV12;
  } else if(strcmp(pix_fmt, "NV21") == 0 || strcmp(pix_fmt, "nv21") == 0) {
    return CNCV_PIX_FMT_NV21;
  } else if(strcmp(pix_fmt, "RGB24") == 0 || strcmp(pix_fmt, "rgb24") == 0) {
    return CNCV_PIX_FMT_RGB;
  } else if(strcmp(pix_fmt, "BGR24") == 0 || strcmp(pix_fmt, "bgr24") == 0) {
    return CNCV_PIX_FMT_BGR;
  } else if(strcmp(pix_fmt, "ARGB") == 0 || strcmp(pix_fmt, "argb") == 0) {
    return CNCV_PIX_FMT_ARGB;
  } else if(strcmp(pix_fmt, "ABGR") == 0 || strcmp(pix_fmt, "abgr") == 0) {
    return CNCV_PIX_FMT_ABGR;
  } else if(strcmp(pix_fmt, "RGBA") == 0 || strcmp(pix_fmt, "rgba") == 0) {
    return CNCV_PIX_FMT_RGBA;
  } else if (strcmp(pix_fmt, "BGRA") == 0 || strcmp(pix_fmt, "bgra") == 0) {
    return CNCV_PIX_FMT_BGRA;
  } else {
    printf("Unsupported pixfmt(%s)\n", pix_fmt);
    return CNCV_PIX_FMT_INVALID;
  }
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
  d_ptr_->src_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size * 2));
  d_ptr_->dst_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size * 2));

  cncvStatus_t cvret;
  cnrtRet_t cnret;
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size * 2);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc mlu buffer failed. Error code:%d\n", cnret);
    return -1;
  }

  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size * 2);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc mlu buffer failed. Error code:%d\n", cnret);
    return -1;
  }

  d_ptr_->src_desc = new cncvImageDescriptor[d_ptr_->batch_size];
  for (int i = 0; i < d_ptr_->batch_size; ++i) { //FIX-ME: multi batch is unnecessary
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

  cvret = cncvGetResizeYuvWorkspaceSize(d_ptr_->batch_size,
                                        d_ptr_->src_desc,
                                        d_ptr_->src_rois,
                                        d_ptr_->dst_desc,
                                        d_ptr_->dst_rois,
                                        &d_ptr_->workspace_size);
  if (cvret != CNCV_STATUS_SUCCESS) {
    printf("Get workspace size failed. Error code:%d\n", cvret);
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->workspace), d_ptr_->workspace_size);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Malloc mlu buffer workspace failed. Error code:%d\n", cnret);
    return -1;
  }
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

int mluop_resize_yuv_exec(HANDLE h,
                          void *input_y, void *input_uv,
                          void *output_y, void *output_uv) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     printf("Sync queue is null.");
     return -1;
  }

  for (int bi = 0; bi < d_ptr_->batch_size; bi++) { //FIX-ME: multi batch is unnecessary
    d_ptr_->src_ptrs_cpu_[bi * 2]     = input_y;
    d_ptr_->src_ptrs_cpu_[bi * 2 + 1] = input_uv;
    d_ptr_->dst_ptrs_cpu_[bi * 2]     = output_y;
    d_ptr_->dst_ptrs_cpu_[bi * 2 + 1] = output_uv;
  }
  cnrtRet_t cnret;
  cncvStatus_t cvret;
  cnret = cnrtMemcpy(d_ptr_->src_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size * 2, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }

  cnret = cnrtMemcpy(d_ptr_->dst_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size * 2, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }

  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_);
  #endif
  cvret = cncvResizeYuv(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        d_ptr_->src_ptrs_mlu_,
                        d_ptr_->dst_desc,
                        d_ptr_->dst_ptrs_mlu_,
                        d_ptr_->dst_rois,
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR);
  if (cvret != CNCV_STATUS_SUCCESS) {
    printf("Resize yuv func failed!\n");
    return -1;
  }
  #ifdef DEBUG
  cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_);
  #endif
  cnret = cnrtSyncQueue(d_ptr_->queue_);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Sync queue failed!\n");
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

int mluop_resize_pad_yuv_exec(HANDLE h,
                              void *input_y, void *input_uv,
                              void *output_y, void *output_uv) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     printf("Sync queue is null.");
     return -1;
  }
  const float EPSINON = 0.00001f;

  for (int bi = 0; bi < d_ptr_->batch_size; bi++) { //FIX-ME: multi batch is unnecessary
    d_ptr_->src_ptrs_cpu_[bi * 2]     = input_y;
    d_ptr_->src_ptrs_cpu_[bi * 2 + 1] = input_uv;
    d_ptr_->dst_ptrs_cpu_[bi * 2]     = output_y;
    d_ptr_->dst_ptrs_cpu_[bi * 2 + 1] = output_uv;
  }
  cnrtRet_t cnret;
  cncvStatus_t cvret;

  cnret = cnrtMemset(output_y, 0, d_ptr_->dst_desc[0].width * d_ptr_->dst_desc[0].height);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memset device value failed. Error code:%d\n", cnret);
    return -1;
  }

  cnret = cnrtMemset(output_uv, 128, d_ptr_->dst_desc[0].width * d_ptr_->dst_desc[0].height / 2);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memset device value failed. Error code:%d\n", cnret);
    return -1;
  }

  cnret = cnrtMemcpy(d_ptr_->src_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size * 2, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }

  cnret = cnrtMemcpy(d_ptr_->dst_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size * 2, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }

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
  cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_);
  #endif
  cvret = cncvResizeYuv(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        d_ptr_->src_ptrs_mlu_,
                        d_ptr_->dst_desc,
                        d_ptr_->dst_ptrs_mlu_,
                        d_ptr_->dst_rois,
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR);
  if (cvret != CNCV_STATUS_SUCCESS) {
    printf("Resize yuv func failed!\n");
    return -1;
  }
  #ifdef DEBUG
  cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_);
  #endif
  cnret = cnrtSyncQueue(d_ptr_->queue_);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Sync queue failed!\n");
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

int mluop_resize_roi_yuv_exec(HANDLE h,
                              void *input_y, void *input_uv,
                              void *output_y, void *output_uv,
                              uint32_t src_roi_x, uint32_t src_roi_y,
                              uint32_t src_roi_w, uint32_t src_roi_h,
                              uint32_t dst_roi_x, uint32_t dst_roi_y,
                              uint32_t dst_roi_w, uint32_t dst_roi_h) {
  cnrtRet_t cnret;
  cncvStatus_t cvret;
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
  for (int bi = 0; bi < d_ptr_->batch_size; ++bi) { //FIX-ME: multi batch is unnecessary
    d_ptr_->src_ptrs_cpu_[bi * 2]     = input_y;
    d_ptr_->src_ptrs_cpu_[bi * 2 + 1] = input_uv;
    d_ptr_->dst_ptrs_cpu_[bi * 2]     = output_y;
    d_ptr_->dst_ptrs_cpu_[bi * 2 + 1] = output_uv;
  }
  cnret = cnrtMemcpy(d_ptr_->src_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size * 2, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }

  cnret = cnrtMemcpy(d_ptr_->dst_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size * 2, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Memcpy host to device failed. Error code:%d\n", cnret);
    return -1;
  }

  #ifdef DEBUG
  gettimeofday(&d_ptr_->start, NULL);
  cnrtPlaceNotifier(d_ptr_->event_begin, d_ptr_->queue_);
  #endif
  cvret = cncvResizeYuv(d_ptr_->handle,
                        d_ptr_->batch_size,
                        d_ptr_->src_desc,
                        d_ptr_->src_rois,
                        d_ptr_->src_ptrs_mlu_,
                        d_ptr_->dst_desc,
                        d_ptr_->dst_ptrs_mlu_,
                        d_ptr_->dst_rois,
                        d_ptr_->workspace_size,
                        d_ptr_->workspace,
                        CNCV_INTER_BILINEAR);
  if (cvret != CNCV_STATUS_SUCCESS) {
    printf("Resize yuv func failed!\n");
    return -1;
  }
  #ifdef DEBUG
  cnrtPlaceNotifier(d_ptr_->event_end, d_ptr_->queue_);
  #endif
  cnret = cnrtSyncQueue(d_ptr_->queue_);
  if (cnret != CNRT_RET_SUCCESS) {
    printf("Sync queue failed!\n");
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

int mluop_resize_yuv_destroy(HANDLE h) {
  CVResizeYUVPrivate *d_ptr_ = static_cast<CVResizeYUVPrivate *>(h);
  if (!d_ptr_) {
    printf("mluop resize yuv op not init\n");
    return 0;
  }
  #ifdef DEBUG
  if (d_ptr_->event_begin) cnrtDestroyNotifier(&d_ptr_->event_begin);
  if (d_ptr_->event_end)   cnrtDestroyNotifier(&d_ptr_->event_end);
  #endif
  if (d_ptr_->src_ptrs_cpu_) {
    free(d_ptr_->src_ptrs_cpu_);
    d_ptr_->src_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->src_ptrs_mlu_) {
    cnrtFree(d_ptr_->src_ptrs_mlu_);
    d_ptr_->src_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->dst_ptrs_cpu_) {
    free(d_ptr_->dst_ptrs_cpu_);
    d_ptr_->dst_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->dst_ptrs_mlu_) {
      cnrtFree(d_ptr_->dst_ptrs_mlu_);
      d_ptr_->dst_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->workspace) {
    cnrtFree(d_ptr_->workspace);
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
    auto ret = cnrtDestroyQueue(d_ptr_->queue_);
    if (ret != CNRT_RET_SUCCESS) {
      printf("Destroy queue failed. Error code: %u", ret);
    }
    d_ptr_->queue_ = nullptr;
  }
  if (d_ptr_->handle) {
    auto ret = cncvDestroy(d_ptr_->handle);
    if (ret != CNCV_STATUS_SUCCESS) {
      printf("Destroy handle failed. Error code: %u", ret);
    }
    d_ptr_->handle = nullptr;
  }
  delete d_ptr_;
  d_ptr_ = nullptr;

  return 0;
}
