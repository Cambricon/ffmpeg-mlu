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
#include "mlu_loging.hpp"

using std::string;
using std::to_string;

#define PRINT_TIME 0

extern void resizeYuvKernel(char** Ydst_gdram, char** UVdst_gdram, \
                            char** Ysrc_gdram, char** UVsrc_gdram, \
                            int s_row, int s_col, int d_row, int d_col, int batch);

inline bool cnrtCheck(cnrtRet_t cnrtret, std::string *estr,
                      const std::string &msg) {
  if (CNRT_RET_SUCCESS != cnrtret) {
    *estr = "CNRT " + msg + " ERRCODE:" + std::to_string(cnrtret);
    return false;
  }
  return true;
}

struct ResizeYUVKernelParam {
  int s_row, s_col, d_row, d_col;
  int batch;
  cnrtKernelInitParam_t init_param = nullptr;
  uint32_t affinity;
};

static float invokeResizeYuvKernel(char** Ydst, char** UVdst, char** srcY, char** srcUV,\
                            ResizeYUVKernelParam* kparam, cnrtFunctionType_t func_type,\
                            cnrtDim3_t dim, cnrtQueue_t queue, string* estr) {
  cnrtKernelParamsBuffer_t params;
  cnrtGetKernelParamsBuffer(&params);
  cnrtKernelParamsBufferAddParam(params, &Ydst, sizeof(char **));
  cnrtKernelParamsBufferAddParam(params, &UVdst, sizeof(char **));
  cnrtKernelParamsBufferAddParam(params, &srcY, sizeof(char **));
  cnrtKernelParamsBufferAddParam(params, &srcUV, sizeof(char **));
  cnrtKernelParamsBufferAddParam(params, &kparam->s_row, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->s_col, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->d_row, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->d_col, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->batch, sizeof(int));

  int ecode;
#if PRINT_TIME
  cnrtNotifier_t event_begin = nullptr;
  cnrtNotifier_t event_end = nullptr;
  if (CNRT_RET_SUCCESS != cnrtCreateNotifier(&event_begin)) {
    std::cout << "cnrtCreateNotifier eventBegin failed" << std::endl;
  }
  if (CNRT_RET_SUCCESS != cnrtCreateNotifier(&event_end)) {
    std::cout << "cnrtCreateNotifier eventEnd failed" << std::endl;
  }
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
  std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
  start_time = end_time = std::chrono::high_resolution_clock::now();
  cnrtPlaceNotifier(event_begin, queue);
#endif

  if (func_type == CNRT_FUNC_TYPE_UNION1) {
    cnrtInvokeParam_t invoke_param;
    invoke_param.invoke_param_type = CNRT_INVOKE_PARAM_TYPE_0;
    invoke_param.cluster_affinity.affinity = &kparam->affinity;
    ecode = cnrtInvokeKernel_V3(reinterpret_cast<void*>(&resizeYuvKernel), kparam->init_param, dim, params, func_type,
                                queue, reinterpret_cast<void*>(&invoke_param));
  } else {
    ecode = cnrtInvokeKernel_V3(reinterpret_cast<void*>(&resizeYuvKernel), kparam->init_param, dim, params, func_type,
                                queue, NULL);
  }
#if PRINT_TIME
  cnrtPlaceNotifier(event_end, queue);
  if (CNRT_RET_SUCCESS != cnrtSyncQueue(queue)) {
    std::cout << "cnrtSyncQueue failed" << std::endl;
  }
  end_time = std::chrono::high_resolution_clock::now();
  float hw_time = 0.0;
  cnrtNotifierDuration(event_begin, event_end, &hw_time);
  std::cout << "------------------------------------------------" << std::endl;
  std::cout << "hardware " << hw_time / 1000.f << "ms" << std::endl;
  std::chrono::duration<double, std::milli> diff = end_time - start_time;
  std::cout << "software " << diff.count() << "ms" << std::endl;
  std::cout << "------------------------------------------------" << std::endl;
  if (event_begin) cnrtDestroyNotifier(&event_begin);
  if (event_end) cnrtDestroyNotifier(&event_end);
#else
  if (CNRT_RET_SUCCESS != cnrtSyncQueue(queue)) {
    std::cout << "cnrtSyncQueue failed" << std::endl;
  }
#endif

  if (CNRT_RET_SUCCESS != ecode) {
    std::cout << "[Resize] cnrtInvokeKernel FAILED. ERRCODE:" << std::endl;;
    cnrtDestroyKernelParamsBuffer(params);
    return -1;
  }
  ecode = cnrtDestroyKernelParamsBuffer(params);
  if (CNRT_RET_SUCCESS != ecode) {
    std::cout << "[Resize] cnrtDestroyKernelParamsBuffer FAILED." << std::endl;
    return -1;
  }

  return 0;
}

struct MluResizeYUVPrivate {
 public:
  int batch_size = 1;
  cnrtDim3_t dim_ = {4, 1, 1};
  cnrtFunctionType_t ftype_ = CNRT_FUNC_TYPE_UNION1;
  cnrtQueue_t queue_ = nullptr;
  ResizeYUVKernelParam* kparam_ = nullptr;

  std::deque<std::pair<void*, void*>> src_yuv_ptrs_cache_;
  void **src_y_ptrs_cpu_ = nullptr, **src_uv_ptrs_cpu_ = nullptr;
  void **src_y_ptrs_mlu_ = nullptr, **src_uv_ptrs_mlu_ = nullptr;
  std::deque<std::pair<void*, void*>> dst_yuv_ptrs_cache_;
  void **dst_y_ptrs_cpu_ = nullptr, **dst_uv_ptrs_cpu_ = nullptr;
  void **dst_y_ptrs_mlu_ = nullptr, **dst_uv_ptrs_mlu_ = nullptr;

  std::string estr_;
};  // MluResziePrivate

int mluop_resize_yuv_invoke_init(HANDLE *h, int input_w, int input_h, int input_stride_in_bytes,
                        int output_w, int output_h, int output_stride_in_bytes, int device_id) {
  MluResizeYUVPrivate *d_ptr_ = new MluResizeYUVPrivate;
  cnrtCreateQueue(&d_ptr_->queue_);
  d_ptr_->src_y_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  d_ptr_->src_uv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  d_ptr_->dst_y_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  d_ptr_->dst_uv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  cnrtRet_t cnret;
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_y_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_uv_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_y_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_uv_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }

  d_ptr_->kparam_ = new ResizeYUVKernelParam;
  d_ptr_->kparam_->s_row = input_h;
  d_ptr_->kparam_->s_col = input_w;
  d_ptr_->kparam_->d_row = output_h;
  d_ptr_->kparam_->d_col = output_w;
  d_ptr_->kparam_->batch = d_ptr_->batch_size;
  cnrtCreateKernelInitParam(&d_ptr_->kparam_->init_param);
  cnrtInitKernelMemory(reinterpret_cast<void*>(&resizeYuvKernel), d_ptr_->kparam_->init_param);
  *h = static_cast<void *>(d_ptr_);
  return 0;
}

int mluop_resize_yuv_invoke_exec(HANDLE h, void *input_y, void *input_uv,
                          void *output_y, void *output_uv) {
  MluResizeYUVPrivate *d_ptr_ = static_cast<MluResizeYUVPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     std::cout << "cnrt queue is nlll." << std::endl;
     return -1;
  }
  d_ptr_->src_yuv_ptrs_cache_.push_back(std::make_pair(input_y, input_uv));
  d_ptr_->dst_yuv_ptrs_cache_.push_back(std::make_pair(output_y, output_uv));
  for (int bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_y_ptrs_cpu_[bi] = d_ptr_->src_yuv_ptrs_cache_.front().first;
    d_ptr_->src_uv_ptrs_cpu_[bi] = d_ptr_->src_yuv_ptrs_cache_.front().second;
    d_ptr_->dst_y_ptrs_cpu_[bi] = d_ptr_->dst_yuv_ptrs_cache_.front().first;
    d_ptr_->dst_uv_ptrs_cpu_[bi] = d_ptr_->dst_yuv_ptrs_cache_.front().second;
    d_ptr_->src_yuv_ptrs_cache_.pop_front();
    d_ptr_->dst_yuv_ptrs_cache_.pop_front();
  }

  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->src_y_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_y_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return false;
  }
  cnret = cnrtMemcpy(d_ptr_->src_uv_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_uv_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return false;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_y_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_y_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return false;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_uv_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_uv_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return false;
  }
  return -1 != invokeResizeYuvKernel(
                reinterpret_cast<char**>(d_ptr_->dst_y_ptrs_mlu_), reinterpret_cast<char**>(d_ptr_->dst_uv_ptrs_mlu_),
                reinterpret_cast<char**>(d_ptr_->src_y_ptrs_mlu_), reinterpret_cast<char**>(d_ptr_->src_uv_ptrs_mlu_),
                d_ptr_->kparam_, d_ptr_->ftype_, d_ptr_->dim_, d_ptr_->queue_, &d_ptr_->estr_);
}

int mluop_resize_yuv_invoke_destroy(HANDLE h) {
  MluResizeYUVPrivate *d_ptr_ = static_cast<MluResizeYUVPrivate *>(h);
  if (d_ptr_) {
    if (d_ptr_->kparam_) {
      if (d_ptr_->kparam_->init_param) {
        cnrtDestroyKernelInitParamAndMemory(d_ptr_->kparam_->init_param);
      }
      delete d_ptr_->kparam_;
      d_ptr_->kparam_ = nullptr;
    }
    if (d_ptr_->src_y_ptrs_cpu_) {
      free(d_ptr_->src_y_ptrs_cpu_);
      d_ptr_->src_y_ptrs_cpu_ = nullptr;
    }
    if (d_ptr_->src_uv_ptrs_cpu_) {
      free(d_ptr_->src_uv_ptrs_cpu_);
      d_ptr_->src_uv_ptrs_cpu_ = nullptr;
    }
    if (d_ptr_->src_y_ptrs_mlu_) {
      cnrtFree(d_ptr_->src_y_ptrs_mlu_);
      d_ptr_->src_y_ptrs_mlu_ = nullptr;
    }
    if (d_ptr_->src_uv_ptrs_mlu_) {
      cnrtFree(d_ptr_->src_uv_ptrs_mlu_);
      d_ptr_->src_uv_ptrs_mlu_ = nullptr;
    }
    d_ptr_->src_yuv_ptrs_cache_.clear();

    if (d_ptr_->dst_y_ptrs_cpu_) {
      free(d_ptr_->dst_y_ptrs_cpu_);
      d_ptr_->dst_y_ptrs_cpu_ = nullptr;
    }
    if (d_ptr_->dst_uv_ptrs_cpu_) {
      free(d_ptr_->dst_uv_ptrs_cpu_);
      d_ptr_->dst_uv_ptrs_cpu_ = nullptr;
    }
    if (d_ptr_->dst_y_ptrs_mlu_) {
      cnrtFree(d_ptr_->dst_y_ptrs_mlu_);
      d_ptr_->dst_y_ptrs_mlu_ = nullptr;
    }
    if (d_ptr_->dst_uv_ptrs_mlu_) {
      cnrtFree(d_ptr_->dst_uv_ptrs_mlu_);
      d_ptr_->dst_uv_ptrs_mlu_ = nullptr;
    }
    d_ptr_->dst_yuv_ptrs_cache_.clear();

    delete d_ptr_;
    d_ptr_ = nullptr;
  }
  return 0;
}
