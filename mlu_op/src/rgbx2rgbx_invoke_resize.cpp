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

using std::string;
using std::to_string;

#define PRINT_TIME 0

#define CNRT_ERROR_CHECK(ret)                                                  \
  if (ret != CNRT_RET_SUCCESS) {                                               \
    fprintf(stderr, "error occur, func: %s, line: %d\n", __func__, __LINE__);  \
    return 0;                                                                 \
  }

extern void MLUBlockKernelResizeRgbx(void **src_gdram,
                                     void **dst_gdram,
                                     uint32_t depth,
                                     uint32_t *src_rois,
                                     uint32_t batch_size,
                                     uint32_t s_height,
                                     uint32_t s_width,
                                     uint32_t s_stride,
                                     uint32_t d_x,
                                     uint32_t d_y,
                                     uint32_t d_col,
                                     uint32_t d_row,
                                     uint32_t d_stride,
                                     uint32_t chn_num);

uint32_t getPixFmtChannelNum(cnPixelFormat pixfmt) {
  if (pixfmt == CN_PIX_FMT_BGR || pixfmt == CN_PIX_FMT_RGB) {
    return 3;
  } else if (pixfmt == CN_PIX_FMT_ABGR || pixfmt == CN_PIX_FMT_ARGB ||
             pixfmt == CN_PIX_FMT_BGRA || pixfmt == CN_PIX_FMT_RGBA) {
    return 4;
  } else if (pixfmt == CN_PIX_FMT_NV12 || pixfmt == CN_PIX_FMT_NV21) {
    return 1;
  } else {
    std::cout << "don't suport pixfmt" << std::endl;
    return 0;
  }
}

uint32_t getSizeOfDepth(cnDepth_t depth) {
  if (depth == CN_DEPTH_8U) {
    return 1;
  } else if (depth == CN_DEPTH_16F) {
    return 2;
  } else if (depth == CN_DEPTH_32F) {
    return 4;
  }
  std::cout << "don't suport depth" << std::endl;
  return 0;
}

struct ResizeRGBXKernelParam {
  uint32_t depth, chn_num;
  uint32_t batch_size, s_height, s_width;
  uint32_t s_stride, d_stride;
  cnrtKernelInitParam_t init_param = nullptr;
  uint32_t affinity;
};

static float invokeResizeRGBXKernel(void** RGBsrc,
                                    uint32_t *src_rois,
                                    void** RGBdst,
                                    uint32_t d_x,
                                    uint32_t d_y,
                                    uint32_t d_col,
                                    uint32_t d_row,
                                    ResizeRGBXKernelParam* kparam,
                                    cnrtFunctionType_t func_type,
                                    cnrtDim3_t dim, cnrtQueue_t queue,
                                    string* estr) {
  cnrtKernelParamsBuffer_t params;
  cnrtGetKernelParamsBuffer(&params);
  cnrtKernelParamsBufferAddParam(params, &RGBsrc, sizeof(void **));
  cnrtKernelParamsBufferAddParam(params, &RGBdst, sizeof(void **));
  cnrtKernelParamsBufferAddParam(params, &kparam->depth, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &src_rois, sizeof(uint32_t *));
  cnrtKernelParamsBufferAddParam(params, &kparam->batch_size, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &kparam->s_height, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &kparam->s_width, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &kparam->s_stride, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &d_x, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &d_y, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &d_col, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &d_row, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &kparam->d_stride, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &kparam->chn_num, sizeof(uint32_t));

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
    ecode = cnrtInvokeKernel_V3(reinterpret_cast<void*>(&MLUBlockKernelResizeRgbx),
                                kparam->init_param, dim, params, func_type,
                                queue, reinterpret_cast<void*>(&invoke_param));
  } else {
    ecode = cnrtInvokeKernel_V3(reinterpret_cast<void*>(&MLUBlockKernelResizeRgbx),
                                kparam->init_param, dim, params, func_type, queue, NULL);
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

struct MluResizeRgbxPrivate {
 public:
  int input_w, input_h;
  int input_stride_in_bytes;
  int output_w, output_h;
  int output_stride_in_bytes;

  int batch_size = 1;

  uint32_t affinity;
  uint32_t depth_size;
  uint32_t pix_chn_num;

  cnDepth_t depth = CN_DEPTH_8U; // init
  cnPixelFormat pix_fmt = CN_PIX_FMT_RGB; // init

  cnrtDim3_t dim_ = {4, 1, 1};
  cnrtQueue_t queue_ = nullptr;
  cnrtFunctionType_t ftype_ = CNRT_FUNC_TYPE_UNION1;
  ResizeRGBXKernelParam* kparam_ = nullptr;

  std::deque<void*> src_rgbx_ptrs_cache_;
  void **src_rgbx_ptrs_cpu_ = nullptr;
  void **src_rgbx_ptrs_mlu_ = nullptr;
  std::deque<void*> dst_rgbx_ptrs_cache_;
  void **dst_rgbx_ptrs_cpu_ = nullptr;
  void **dst_rgbx_ptrs_mlu_ = nullptr;

  uint32_t *src_rois_cpu;
  uint32_t *src_rois_mlu;

  std::string estr_;
};  // MluReszieRgbxPrivate

int mluop_resize_rgbx_invoke_init(HANDLE *h,
                                  int input_w,
                                  int input_h,
                                  int output_w,
                                  int output_h,
                                  cnPixelFormat pix_fmt,
                                  cnDepth_t depth) {
  MluResizeRgbxPrivate *d_ptr_ = new MluResizeRgbxPrivate;

  cnrtRet_t cnret;
  cnrtCreateQueue(&d_ptr_->queue_);

  d_ptr_->depth = depth;
  d_ptr_->pix_fmt = pix_fmt;
  d_ptr_->input_h = input_h;
  d_ptr_->input_w = input_w;
  d_ptr_->output_h = output_h;
  d_ptr_->output_w = output_w;

  d_ptr_->depth_size = getSizeOfDepth(depth);
  d_ptr_->pix_chn_num = getPixFmtChannelNum(pix_fmt);
  d_ptr_->input_stride_in_bytes = d_ptr_->input_w * d_ptr_->pix_chn_num * d_ptr_->depth_size;
  d_ptr_->output_stride_in_bytes = d_ptr_->output_w * d_ptr_->pix_chn_num * d_ptr_->depth_size;
  d_ptr_->src_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  d_ptr_->dst_rgbx_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
  d_ptr_->src_rois_cpu = (uint32_t *)malloc(d_ptr_->batch_size * 4 * sizeof(uint32_t));
  CNRT_CHECK(cnrtMalloc((void **)(&d_ptr_->src_rois_mlu),
                        d_ptr_->batch_size * 4 * sizeof(uint32_t)));
  for (int i = 0; i < d_ptr_->batch_size; i ++) {
    d_ptr_->src_rois_cpu[i * 4 + 0] = 0;
    d_ptr_->src_rois_cpu[i * 4 + 1] = 0;
    d_ptr_->src_rois_cpu[i * 4 + 2] = d_ptr_->input_w;
    d_ptr_->src_rois_cpu[i * 4 + 3] = d_ptr_->input_h;
  }
  CNRT_CHECK(cnrtMemcpy(d_ptr_->src_rois_mlu, d_ptr_->src_rois_cpu,
                        d_ptr_->batch_size * 4 * sizeof(uint32_t),
                        CNRT_MEM_TRANS_DIR_HOST2DEV));

  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_rgbx_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_rgbx_ptrs_mlu_), sizeof(char*) * d_ptr_->batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }

  d_ptr_->kparam_ = new ResizeRGBXKernelParam;
  d_ptr_->kparam_->depth = d_ptr_->depth_size;
  d_ptr_->kparam_->batch_size = d_ptr_->batch_size;
  d_ptr_->kparam_->s_height = d_ptr_->input_h;
  d_ptr_->kparam_->s_width = d_ptr_->input_w;
  d_ptr_->kparam_->s_stride = d_ptr_->input_stride_in_bytes;
  d_ptr_->kparam_->d_stride = d_ptr_->output_stride_in_bytes;
  d_ptr_->kparam_->chn_num = d_ptr_->pix_chn_num;

  cnret = cnrtCreateKernelInitParam(&d_ptr_->kparam_->init_param);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "cnrtCreateKernelInitParam failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtInitKernelMemory(reinterpret_cast<void*>(&MLUBlockKernelResizeRgbx), d_ptr_->kparam_->init_param);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "cnrtInitKernelMemory failed. Error code:" << std::endl;
    return -1;
  }

  *h = static_cast<void *>(d_ptr_);
  return 0;
}

int mluop_resize_rgbx_invoke_exec(HANDLE h,
                                 void *input,
                                 void *output,
                                 uint32_t d_x,
                                 uint32_t d_y,
                                 uint32_t d_w,
                                 uint32_t d_h) {
  MluResizeRgbxPrivate *d_ptr_ = static_cast<MluResizeRgbxPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     std::cout << "cnrt queue is nlll." << std::endl;
     return -1;
  }
  d_ptr_->src_rgbx_ptrs_cache_.push_back(input); // input is mlu src ptr, ..._ptrs_cache_ is cpu ptr for batch address
  d_ptr_->dst_rgbx_ptrs_cache_.push_back(output); //
  for (int bi = 0; bi < d_ptr_->batch_size; ++bi) {
    d_ptr_->src_rgbx_ptrs_cpu_[bi] = d_ptr_->src_rgbx_ptrs_cache_.front();
    d_ptr_->dst_rgbx_ptrs_cpu_[bi] = d_ptr_->dst_rgbx_ptrs_cache_.front();
    d_ptr_->src_rgbx_ptrs_cache_.pop_front();
    d_ptr_->dst_rgbx_ptrs_cache_.pop_front();
  }

  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->src_rgbx_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_rgbx_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_rgbx_ptrs_cpu_),
      sizeof(char*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return -1;
  }
  float r_ret = invokeResizeRGBXKernel(
                static_cast<void **>(d_ptr_->src_rgbx_ptrs_mlu_),
                reinterpret_cast<uint32_t *>(d_ptr_->src_rois_mlu),
                static_cast<void **>(d_ptr_->dst_rgbx_ptrs_mlu_),
                d_x, d_y, d_w, d_h,
                d_ptr_->kparam_, d_ptr_->ftype_, d_ptr_->dim_,
                d_ptr_->queue_, &d_ptr_->estr_);

  return r_ret;
}

int mluop_resize_rgbx_invoke_destroy(HANDLE h) {
  MluResizeRgbxPrivate *d_ptr_ = static_cast<MluResizeRgbxPrivate *>(h);
  if (d_ptr_) {
    if (d_ptr_->kparam_) {
      if (d_ptr_->kparam_->init_param) {
        cnrtRet_t cnret = cnrtDestroyKernelInitParamAndMemory(d_ptr_->kparam_->init_param);
        if (cnret != CNRT_RET_SUCCESS) {
          std::cout << "cnrtDestroyKernelInitParamAndMemory failed. Error code: %u" << std::endl;
          return -1;
        }
      }
      delete d_ptr_->kparam_;
      d_ptr_->kparam_ = nullptr;
    }
    if (d_ptr_->src_rgbx_ptrs_cpu_) {
      free(d_ptr_->src_rgbx_ptrs_cpu_);
      d_ptr_->src_rgbx_ptrs_cpu_ = nullptr;
    }
    if (d_ptr_->src_rgbx_ptrs_mlu_) {
      cnrtFree(d_ptr_->src_rgbx_ptrs_mlu_);
      d_ptr_->src_rgbx_ptrs_mlu_ = nullptr;
    }
    d_ptr_->src_rgbx_ptrs_cache_.clear();

    if (d_ptr_->dst_rgbx_ptrs_cpu_) {
      free(d_ptr_->dst_rgbx_ptrs_cpu_);
      d_ptr_->dst_rgbx_ptrs_cpu_ = nullptr;
    }
    if (d_ptr_->dst_rgbx_ptrs_mlu_) {
      cnrtFree(d_ptr_->dst_rgbx_ptrs_mlu_);
      d_ptr_->dst_rgbx_ptrs_mlu_ = nullptr;
    }
    d_ptr_->dst_rgbx_ptrs_cache_.clear();

    if (d_ptr_->src_rois_cpu) {
      free(d_ptr_->src_rois_cpu);
      d_ptr_->src_rois_cpu = nullptr;
    }
    if (d_ptr_->src_rois_mlu) {
      cnrtFree(d_ptr_->src_rois_mlu);
      d_ptr_->src_rois_mlu = nullptr;
    }

    if (d_ptr_->queue_) {
      auto ret = cnrtDestroyQueue(d_ptr_->queue_);
      if (ret != CNRT_RET_SUCCESS) {
        std::cout << "Destroy queue failed. Error code: %u" << std::endl;
        return -1;
      }
      d_ptr_->queue_ = nullptr;
    }
    delete d_ptr_;
    d_ptr_ = nullptr;
  }
  return 0;
}
