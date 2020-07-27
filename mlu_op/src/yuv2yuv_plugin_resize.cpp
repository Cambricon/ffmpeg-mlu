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
#include <sys/time.h>

#include "cnml.h"
#include "cnplugin.h"
#include "cnrt.h"
#include "mlu_loging.hpp"
#include "mluop.h"

#define PRINT_TIME 0

inline bool cnrtCheck(cnrtRet_t cnrtret, std::string *estr,
                      const std::string &msg) {
  if (CNRT_RET_SUCCESS != cnrtret) {
    *estr = "CNRT " + msg + " ERRCODE:" + std::to_string(cnrtret);
    return false;
  }
  return true;
}
inline bool cnmlCheck(cnmlStatus_t cnmlret, std::string *estr,
                      const std::string &msg) {
  if (CNML_STATUS_SUCCESS != cnmlret) {
    *estr = "CNML " + msg + " ERRCODE:" + std::to_string(cnmlret);
    return false;
  }
  return true;
}

using std::string;
using std::to_string;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// internal functions
struct ResizeYuv2Yuv {
  void **input_addrs = nullptr;
  void **output_addrs = nullptr;
  cnmlPluginResizeAndColorCvtParam_t param = nullptr;
  cnmlBaseOp_t op = nullptr;
  cnmlTensor_t *cnml_input_ptr = nullptr;
  cnmlTensor_t *cnml_output_ptr = nullptr;
  // enum
  int batch_size = 1;
  cnmlCoreVersion_t version = CNML_MLU270;
#if PRINT_TIME
  cnrtNotifier_t event_begin = nullptr;
  cnrtNotifier_t event_end = nullptr;
#endif
};

static bool createParam(ResizeYuv2Yuv *yuv2yuv, cnmlCoreVersion_t version,
                        int src_w, int src_h, int dst_w, int dst_h,
                        int batch_size, string *estr) {
  if (!yuv2yuv) {
    *estr = "[CreateParam] yuv2yuv is nullptr";
    return false;
  }

  yuv2yuv->batch_size = batch_size;
  yuv2yuv->version = version;
  ioParams mode;
  auto cnmlret = cnmlCreatePluginResizeYuvToYuvOpParam(
      &(yuv2yuv->param), src_h, src_w, dst_h, dst_w, mode, yuv2yuv->version);

  if (!cnmlCheck(cnmlret, estr,
                 "Create Plugin ResizeYuv2Yuv Op param failed.")) {
    return false;
  }
  return true;
}

void freeTensorPtr(ResizeYuv2Yuv *yuv2yuv) {
  if (yuv2yuv) {
    if (yuv2yuv->cnml_input_ptr) {
      free(yuv2yuv->cnml_input_ptr);
    }
    if (yuv2yuv->cnml_output_ptr) {
      free(yuv2yuv->cnml_output_ptr);
    }
  }
}

static void initTensorPtr(ResizeYuv2Yuv *yuv2yuv) {
  if (yuv2yuv) {
    auto param = yuv2yuv->param;
    yuv2yuv->cnml_input_ptr = reinterpret_cast<cnmlTensor_t *>(
        malloc(sizeof(cnmlTensor_t) * param->input_num));
    yuv2yuv->cnml_output_ptr = reinterpret_cast<cnmlTensor_t *>(
        malloc(sizeof(cnmlTensor_t) * param->output_num));
  }
}

static bool destroyTensor(ResizeYuv2Yuv *yuv2yuv, string *estr) {
  if (!yuv2yuv) {
    *estr = "[DestroyTensor] yuv2yuv is nullptr";
    return false;
  }
  auto param = yuv2yuv->param;
  bool success = true;
  for (int i = 0; i < param->input_num; i++) {
    if (yuv2yuv->cnml_input_ptr[i]) {
      success = cnmlCheck(cnmlDestroyTensor(&yuv2yuv->cnml_input_ptr[i]), estr,
                          "Destroy input Tensor failed.");
    }
  }
  for (int i = 0; i < param->output_num; i++) {
    if (yuv2yuv->cnml_output_ptr[i]) {
      success = cnmlCheck(cnmlDestroyTensor(&yuv2yuv->cnml_output_ptr[i]), estr,
                          "Destroy output Tensor failed.");
    }
  }
  return success;
}

static bool createTensor(ResizeYuv2Yuv *yuv2yuv, string *estr) {
  if (!yuv2yuv) {
    *estr = "[CreateTensor] yuv2yuv is nullptr";
    return false;
  }
  auto param = yuv2yuv->param;

  if (param->input_num != 2 || param->output_num != 2) {
    *estr = "Input number or output number is not 2. Input num: " +
            to_string(param->input_num) +
            " Output num: " + to_string(param->input_num);
    return false;
  }

  int shape[4] = {yuv2yuv->batch_size, 1, 1, 1};
  cnmlDataType_t dt = CNML_DATA_INT32;

  for (uint8_t i = 0; i < param->input_num; i++) {
    // input tensor
    auto cnmlret =
        cnmlCreateTensor_V2(&(yuv2yuv->cnml_input_ptr[i]), CNML_TENSOR);
    if (!cnmlCheck(cnmlret, estr, "Create input tensor failed."))
      return false;
    cnmlret = cnmlSetTensorShape(yuv2yuv->cnml_input_ptr[i], 4, shape);
    if (!cnmlCheck(cnmlret, estr, "Set input tensor shape failed."))
      return false;
    cnmlret = cnmlSetTensorDataType(yuv2yuv->cnml_input_ptr[i], dt);
    if (!cnmlCheck(cnmlret, estr, "Set input tensor data type failed."))
      return false;
    // output tensor
    cnmlret = cnmlCreateTensor_V2(&(yuv2yuv->cnml_output_ptr[i]), CNML_TENSOR);
    if (!cnmlCheck(cnmlret, estr, "Create output tensor failed."))
      return false;
    cnmlret = cnmlSetTensorShape(yuv2yuv->cnml_output_ptr[i], 4, shape);
    if (!cnmlCheck(cnmlret, estr, "Set output tensor shape failed."))
      return false;
    cnmlret = cnmlSetTensorDataType(yuv2yuv->cnml_output_ptr[i], dt);
    if (!cnmlCheck(cnmlret, estr, "Set output tensor shape failed."))
      return false;
  }

  return true;
}

static bool createAndCompileOp(const int &core_limit, ResizeYuv2Yuv *yuv2yuv,
                               string *estr) {
  if (!yuv2yuv) {
    *estr = "[CreateAndCompileOp] yuv2yuv is nullptr";
    return false;
  }
  auto param = yuv2yuv->param;
  initTensorPtr(yuv2yuv);
  if (!createTensor(yuv2yuv, estr)) {
    return false;
  }
  auto cnmlret = cnmlCreatePluginResizeYuvToYuvOp(
      &(yuv2yuv->op), param, yuv2yuv->cnml_input_ptr, yuv2yuv->cnml_output_ptr);
  if (!cnmlCheck(cnmlret, estr, "Create Plugin ResizeYuvToYuv Op failed."))
    return false;

  cnmlret = cnmlCompileBaseOp(yuv2yuv->op, yuv2yuv->version, core_limit);
  if (!cnmlCheck(cnmlret, estr, "Compile Plugin ResizeYuvToYuv Op failed."))
    return false;

  return true;
}

static void freeIoAddrsPtr(ResizeYuv2Yuv *yuv2yuv) {
  if (yuv2yuv) {
    if (yuv2yuv->input_addrs) {
      free(yuv2yuv->input_addrs);
    }
    if (yuv2yuv->output_addrs) {
      free(yuv2yuv->output_addrs);
    }
  }
}

static void initIOAddrsPtr(ResizeYuv2Yuv *yuv2yuv) {
  if (yuv2yuv) {
    yuv2yuv->input_addrs = reinterpret_cast<void **>(
        malloc(sizeof(void *) * yuv2yuv->param->input_num));
    yuv2yuv->output_addrs = reinterpret_cast<void **>(
        malloc(sizeof(void *) * yuv2yuv->param->output_num));
  }
}

static bool destroyResizeYuv2Yuv(ResizeYuv2Yuv *yuv2yuv, string *estr) {
  bool success = true;
  if (yuv2yuv) {
#if PRINT_TIME
    if (yuv2yuv->event_begin) {
      success = cnrtCheck(cnrtDestroyNotifier(&yuv2yuv->event_begin), estr,
                          "Destroy event begin failed.");
    }
    if (yuv2yuv->event_end) {
      success = cnrtCheck(cnrtDestroyNotifier(&yuv2yuv->event_end), estr,
                          "Destroy event end failed.");
    }
#endif
    success = destroyTensor(yuv2yuv, estr);
    freeTensorPtr(yuv2yuv);

    freeIoAddrsPtr(yuv2yuv);

    if (yuv2yuv->op) {
      success = cnmlCheck(cnmlDestroyBaseOp(&(yuv2yuv->op)), estr,
                          "Destroy resize yuv2yuv op failed.");
    }
    if (yuv2yuv->param) {
      success =
          cnmlCheck(cnmlDestroyPluginResizeYuvToYuvOpParam(&yuv2yuv->param),
                    estr, "Destroy resize yuv2yuv param failed.");
    }
    delete yuv2yuv;
  }
  success = cnmlCheck(cnmlExit(), estr, "Exit failed.");
  return success;
}

static bool createResizeYuv2Yuv(ResizeYuv2Yuv **yuv2yuv_ptr, int core_num,
                                cnmlCoreVersion_t version, int src_w, int src_h,
                                int dst_w, int dst_h, int batch_size,
                                string *estr) {
  (*yuv2yuv_ptr) = new ResizeYuv2Yuv;

  if (!*yuv2yuv_ptr) {
    *estr = "Create ResizeYuv2Yuv pointer failed";
    return false;
  }

  if (!cnmlCheck(cnmlInit(0), estr, "Init failed"))
    return false;

#if PRINT_TIME
  if (!cnrtCheck(cnrtCreateNotifier(&(*yuv2yuv_ptr)->event_begin), estr,
                 "create notifier event_begin failed.")) {
    return false;
  }
  if (!cnrtCheck(cnrtCreateNotifier(&(*yuv2yuv_ptr)->event_end), estr,
                 "create notifier event_end failed.")) {
    return false;
  }
#endif

  if (!createParam(*yuv2yuv_ptr, version, src_w, src_h, dst_w, dst_h,
                   batch_size, estr))
    return false;
  if (!createAndCompileOp(core_num, *yuv2yuv_ptr, estr))
    return false;

  initIOAddrsPtr(*yuv2yuv_ptr);
  return true;
}

static bool computeResizeYuv2Yuv(ResizeYuv2Yuv *yuv2yuv, void *dst_y,
                                 void *dst_uv, void *src_y, void *src_uv,
                                 cnrtQueue_t queue, string *estr) {
  if (!yuv2yuv) {
    *estr = "[ComputeResizeYuv2Yuv] yuv2yuv is nullptr";
    return false;
  }
  yuv2yuv->input_addrs[0] = src_y;
  yuv2yuv->input_addrs[1] = src_uv;
  yuv2yuv->output_addrs[0] = dst_y;
  yuv2yuv->output_addrs[1] = dst_uv;
#if PRINT_TIME
  cnrtPlaceNotifier(yuv2yuv->event_begin, queue);
  auto start_tp = std::chrono::high_resolution_clock::now();
#endif
  auto cnmlret = cnmlComputePluginResizeYuvToYuvOpForward(
      yuv2yuv->op, yuv2yuv->param, yuv2yuv->cnml_input_ptr,
      yuv2yuv->input_addrs, yuv2yuv->cnml_output_ptr, yuv2yuv->output_addrs,
      queue);
  if (!cnmlCheck(cnmlret, estr, "Compute Plugin ResizeYuv2Yuv Op failed.")) {
    return false;
  }
#if PRINT_TIME
  cnrtPlaceNotifier(yuv2yuv->event_end, queue);
#endif
  // sync queue
  int success = cnrtCheck(cnrtSyncQueue(queue), estr, "Sync queue failed.");
#if PRINT_TIME
  auto end_tp = std::chrono::high_resolution_clock::now();
  float hw_time = 0.f;
  cnrtNotifierDuration(yuv2yuv->event_begin, yuv2yuv->event_end, &hw_time);
  std::cout << "hardware " << hw_time / 1000.f << "ms" << std::endl;
  std::chrono::duration<double, std::milli> diff = end_tp - start_tp;
  std::cout << "software " << diff.count() << "ms" << std::endl;
#endif
  return success;
}

////////////////////////////////////////////////////////////////////////////////////
// external supports
typedef struct {
  cnrtQueue_t queue_ = nullptr;
  ResizeYuv2Yuv *yuv2yuv_ = nullptr;
  std::deque<std::pair<void *, void *>> src_yuv_ptrs_cache_;
  std::deque<std::pair<void *, void *>> dst_yuv_ptrs_cache_;
  void *src_y_ptrs_cpu_ = nullptr, *src_uv_ptrs_cpu_ = nullptr;
  void *src_y_ptrs_mlu_ = nullptr, *src_uv_ptrs_mlu_ = nullptr;
  void *dst_y_ptrs_cpu_ = nullptr, *dst_uv_ptrs_cpu_ = nullptr;
  void *dst_y_ptrs_mlu_ = nullptr, *dst_uv_ptrs_mlu_ = nullptr;
  std::string estr_;
  int batch_size_;
} Yuv2YuvResizeContext_t;

int mluop_resize_yuv_plugin_init(HANDLE *h, int input_w, int input_h,
                          int input_stride_in_bytes, int output_w, int output_h,
                          int output_stride_in_bytes, int device_id) {
  Yuv2YuvResizeContext_t *ctx = new Yuv2YuvResizeContext_t;
  cnrtCreateQueue(&ctx->queue_);
  ctx->batch_size_ = 1;
  // malloc cpu
  ctx->src_y_ptrs_cpu_ = malloc(sizeof(void *) * ctx->batch_size_);
  ctx->src_uv_ptrs_cpu_ = malloc(sizeof(void *) * ctx->batch_size_);
  ctx->dst_y_ptrs_cpu_ = malloc(sizeof(void *) * ctx->batch_size_);
  ctx->dst_uv_ptrs_cpu_ = malloc(sizeof(void *) * ctx->batch_size_);
  // malloc mlu
  cnrtRet_t cnret =
      cnrtMalloc(&ctx->src_y_ptrs_mlu_, sizeof(void *) * ctx->batch_size_);
  if (!cnrtCheck(cnret, &ctx->estr_, "Malloc src y mlu buffer failed."))
    return -1;
  cnret = cnrtMalloc(&ctx->src_uv_ptrs_mlu_, sizeof(void *) * ctx->batch_size_);
  if (!cnrtCheck(cnret, &ctx->estr_, "Malloc src uv mlu buffer failed."))
    return -1;
  cnret = cnrtMalloc(&ctx->dst_y_ptrs_mlu_, sizeof(void *) * ctx->batch_size_);
  if (!cnrtCheck(cnret, &ctx->estr_, "Malloc dst y mlu buffer failed."))
    return -1;
  cnret = cnrtMalloc(&ctx->dst_uv_ptrs_mlu_, sizeof(void *) * ctx->batch_size_);
  if (!cnrtCheck(cnret, &ctx->estr_, "Malloc dst uv mlu buffer failed."))
    return -1;

  cnrtDeviceInfo_t info;
  bool success = false;
  int dev_id = 0;
  if (device_id > 0) {
    dev_id = device_id;
  }
  cnret = cnrtGetDeviceInfo(&info, dev_id);
  switch (info.core_version) {
  case CNRT_MLU270:
    success =
        createResizeYuv2Yuv(&ctx->yuv2yuv_, 4, CNML_MLU270, input_w, input_h,
                            output_w, output_h, ctx->batch_size_, &ctx->estr_);
    break;
  case CNRT_MLU220:
    success =
        createResizeYuv2Yuv(&ctx->yuv2yuv_, 4, CNML_MLU220, input_w, input_h,
                            output_w, output_h, ctx->batch_size_, &ctx->estr_);
    break;
  default:
    logging::ERROR("Not support this case");
  }
  if (!success) {
    std::cout << ctx->estr_ << std::endl;
    if (ctx->yuv2yuv_) {
      if (!::destroyResizeYuv2Yuv(ctx->yuv2yuv_, &ctx->estr_)) {
        logging::ERROR("DestroyResizeYuv2Yuv Error: " + ctx->estr_);
      }
      ctx->yuv2yuv_ = nullptr;
    }
  }
  *h = static_cast<void *>(ctx);
  if (!success) return -1;
  return 0;
}

int mluop_resize_yuv_plugin_exec(HANDLE h, void *input_y, void *input_uv,
                          void *output_y, void *output_uv) {

  Yuv2YuvResizeContext_t *ctx = static_cast<Yuv2YuvResizeContext_t *>(h);
  if (nullptr == ctx->queue_) {
    logging::ERROR("cnrt queue is null.");
    return -1;
  }
  ctx->src_yuv_ptrs_cache_.push_back(std::make_pair(input_y, input_uv));
  ctx->dst_yuv_ptrs_cache_.push_back(std::make_pair(output_y, output_uv));

  if (static_cast<int>(ctx->src_yuv_ptrs_cache_.size()) < ctx->batch_size_ ||
      static_cast<int>(ctx->dst_yuv_ptrs_cache_.size()) < ctx->batch_size_) {
    logging::ERROR("Batchsize is " + std::to_string(ctx->batch_size_) +
                    ", but only has input: " + std::to_string(ctx->src_yuv_ptrs_cache_.size()) +
                    ", output: " + std::to_string(ctx->dst_yuv_ptrs_cache_.size()));
    return -1;
  }
  for (int bi = 0; bi < ctx->batch_size_; ++bi) {
    reinterpret_cast<void **>(ctx->src_y_ptrs_cpu_)[bi] =
        ctx->src_yuv_ptrs_cache_.front().first;
    reinterpret_cast<void **>(ctx->src_uv_ptrs_cpu_)[bi] =
        ctx->src_yuv_ptrs_cache_.front().second;
    reinterpret_cast<void **>(ctx->dst_y_ptrs_cpu_)[bi] =
        ctx->dst_yuv_ptrs_cache_.front().first;
    reinterpret_cast<void **>(ctx->dst_uv_ptrs_cpu_)[bi] =
        ctx->dst_yuv_ptrs_cache_.front().second;
    ctx->src_yuv_ptrs_cache_.pop_front();
    ctx->dst_yuv_ptrs_cache_.pop_front();
  }

  cnrtRet_t cnret = cnrtMemcpy(ctx->src_y_ptrs_mlu_, ctx->src_y_ptrs_cpu_,
                               sizeof(void *) * ctx->batch_size_,
                               CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (!cnrtCheck(cnret, &ctx->estr_,
                 "Memcpy src y from host to device failed."))
    return -1;
  cnret = cnrtMemcpy(ctx->src_uv_ptrs_mlu_, ctx->src_uv_ptrs_cpu_,
                     sizeof(void *) * ctx->batch_size_,
                     CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (!cnrtCheck(cnret, &ctx->estr_,
                 "Memcpy src uv from host to device failed."))
    return -1;
  cnret = cnrtMemcpy(ctx->dst_y_ptrs_mlu_, ctx->dst_y_ptrs_cpu_,
                     sizeof(void *) * ctx->batch_size_,
                     CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (!cnrtCheck(cnret, &ctx->estr_,
                 "Memcpy dst y from host to device failed."))
    return -1;
  cnret = cnrtMemcpy(ctx->dst_uv_ptrs_mlu_, ctx->dst_uv_ptrs_cpu_,
                     sizeof(void *) * ctx->batch_size_,
                     CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (!cnrtCheck(cnret, &ctx->estr_,
                 "Memcpy dst uv from host to device failed."))
    return -1;
  bool ret = false;
  ret = ::computeResizeYuv2Yuv(
      ctx->yuv2yuv_, ctx->dst_y_ptrs_mlu_, ctx->dst_uv_ptrs_mlu_,
      ctx->src_y_ptrs_mlu_, ctx->src_uv_ptrs_mlu_, ctx->queue_, &ctx->estr_);
  if (!ret) return -1;
  return 0;
}

int mluop_resize_yuv_plugin_destroy(HANDLE h) {
  Yuv2YuvResizeContext_t *ctx = static_cast<Yuv2YuvResizeContext_t *>(h);
  if (ctx) {
    if (ctx->yuv2yuv_) {
      if (!::destroyResizeYuv2Yuv(ctx->yuv2yuv_, &ctx->estr_)) {
        logging::ERROR("DestroyResizeYuv2Yuv Error: " + ctx->estr_);
        return -1;
      }
      ctx->yuv2yuv_ = nullptr;
    }
    if (ctx->src_y_ptrs_cpu_) {
      free(ctx->src_y_ptrs_cpu_);
      ctx->src_y_ptrs_cpu_ = nullptr;
    }
    if (ctx->src_uv_ptrs_cpu_) {
      free(ctx->src_uv_ptrs_cpu_);
      ctx->src_uv_ptrs_cpu_ = nullptr;
    }
    if (ctx->dst_y_ptrs_cpu_) {
      free(ctx->dst_y_ptrs_cpu_);
      ctx->dst_y_ptrs_cpu_ = nullptr;
    }
    if (ctx->dst_uv_ptrs_cpu_) {
      free(ctx->dst_uv_ptrs_cpu_);
      ctx->dst_uv_ptrs_cpu_ = nullptr;
    }
    if (ctx->src_y_ptrs_mlu_) {
      cnrtFree(ctx->src_y_ptrs_mlu_);
      ctx->src_y_ptrs_mlu_ = nullptr;
    }
    if (ctx->src_uv_ptrs_mlu_) {
      cnrtFree(ctx->src_uv_ptrs_mlu_);
      ctx->src_uv_ptrs_mlu_ = nullptr;
    }
    if (ctx->dst_y_ptrs_mlu_) {
      cnrtFree(ctx->dst_y_ptrs_mlu_);
      ctx->dst_y_ptrs_mlu_ = nullptr;
    }
    if (ctx->dst_uv_ptrs_mlu_) {
      cnrtFree(ctx->dst_uv_ptrs_mlu_);
      ctx->dst_uv_ptrs_mlu_ = nullptr;
    }
    ctx->src_yuv_ptrs_cache_.clear();
    ctx->dst_yuv_ptrs_cache_.clear();
    if (ctx->queue_) {
      auto ret = cnrtDestroyQueue(ctx->queue_);
      if (ret != CNRT_RET_SUCCESS) {
        logging::ERROR("Destroy queue failed. Error !");
        return -1;
      }
      ctx->queue_ = nullptr;
    }
    delete ctx;
    ctx = nullptr;
  }
  return 0;
}