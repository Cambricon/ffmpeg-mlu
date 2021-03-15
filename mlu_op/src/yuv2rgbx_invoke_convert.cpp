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
#include "string.h"
#include "mluop.h"

#define CI 64
using std::string;
using std::to_string;

#define PRINT_TIME 0

extern void MLUUnion1KernelYuv420spToRgb(uint8_t **src_gdram,
                                         uint8_t **dst_gdram,
                                         int16_t *conv_kernel_gdram,
                                         float *bias_gdram,
                                         const uint32_t height,
                                         const uint32_t width,
                                         const uint32_t src_y_stride,
                                         const uint32_t src_uv_stride,
                                         const uint32_t dst_stride,
                                         const uint32_t batch_size,
                                         const uint32_t ci,
                                         const uint32_t out_chn);

#define CNRT_ERROR_CHECK(ret)                                                  \
  if (ret != CNRT_RET_SUCCESS) {                                               \
    fprintf(stderr, "error occur, func: %s, line: %d\n", __func__, __LINE__);  \
    return 0;                                                                 \
  }

inline bool cnrtCheck(cnrtRet_t cnrtret,
                      std::string *estr,
                      const std::string &msg) {
  if (CNRT_RET_SUCCESS != cnrtret) {
    *estr = "CNRT " + msg + " ERRCODE:" + std::to_string(cnrtret);
    return false;
  }
  return true;
}

struct ConvertYUV2RGBXKernelParam {
  int16_t *conv_kernel_gdram;
  float *bias_gdram;
  uint32_t height, width;
  uint32_t src_y_stride, src_uv_stride, dst_stride;
  uint32_t batch_size;
  uint32_t ci;
  uint32_t out_chn;

  cnrtKernelInitParam_t init_param = nullptr;
  uint32_t affinity;
};

static float invokeConvertYUV2RGBXKernel(uint8_t **srcYUV, uint8_t **RGBXdst,
                                         ConvertYUV2RGBXKernelParam *kparam,
                                         cnrtFunctionType_t func_type,cnrtDim3_t dim,
                                         cnrtQueue_t queue,string* estr) {
  cnrtKernelParamsBuffer_t params;
  cnrtGetKernelParamsBuffer(&params);
  cnrtKernelParamsBufferAddParam(params, &srcYUV, sizeof(uint8_t **));
  cnrtKernelParamsBufferAddParam(params, &RGBXdst, sizeof(uint8_t **));
  cnrtKernelParamsBufferAddParam(params,
                                 &kparam->conv_kernel_gdram, sizeof(int16_t *));
  cnrtKernelParamsBufferAddParam(params, &kparam->bias_gdram, sizeof(float *));
  cnrtKernelParamsBufferAddParam(params, &kparam->height, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &kparam->width, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params,
                                 &kparam->src_y_stride, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params,
                                 &kparam->src_uv_stride, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &kparam->dst_stride, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &kparam->batch_size, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &kparam->ci, sizeof(uint32_t));
  cnrtKernelParamsBufferAddParam(params, &kparam->out_chn, sizeof(uint32_t));

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
    ecode = cnrtInvokeKernel_V3(
            reinterpret_cast<void*>(&MLUUnion1KernelYuv420spToRgb),
            kparam->init_param, dim, params, func_type, queue,
            reinterpret_cast<void*>(&invoke_param));
  } else {
    ecode = cnrtInvokeKernel_V3(
            reinterpret_cast<void*>(&MLUUnion1KernelYuv420spToRgb),
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
    std::cout << "[Convert] cnrtInvokeKernel FAILED. ERRCODE:" << std::endl;
    cnrtDestroyKernelParamsBuffer(params);
    return -1;
  }
  ecode = cnrtDestroyKernelParamsBuffer(params);
  if (CNRT_RET_SUCCESS != ecode) {
    std::cout << "[Convert] cnrtDestroyKernelParamsBuffer FAILED." << std::endl;
    return -1;
  }

  return 0;
}

struct MluYUV2RGBPrivate {
 public:
  int input_w, input_h;
  int input_y_stride_in_bytes;
  int input_uv_stride_in_bytes;
  int output_w, output_h;
  int output_stride_in_bytes;

  int batch_size = 1;
  int16_t *filter_mlu;
  float *bias_mlu;

  void *workspace;

  uint32_t affinity;
  uint32_t depth_size;
  uint32_t src_pix_chn_num;
  uint32_t dst_pix_chn_num;

  cnDepth_t depth; // init
  cnPixelFormat src_pixfmt;
  cnPixelFormat dst_pixfmt;
  cnColorSpace color_space;
  ConvertYUV2RGBXKernelParam* kparam_;

  cnrtDim3_t dim_ = {4, 1, 1};
  cnrtQueue_t queue_;
  cnrtFunctionType_t ftype_ = CNRT_FUNC_TYPE_UNION1;

  std::deque<std::pair<void*, void*>> src_yuv_ptrs_cache_;
  void **src_yuv_ptrs_cpu_;
  void **src_yuv_ptrs_mlu_;
  std::deque<void*> dst_rgbx_ptrs_cache_;
  void **dst_rgbx_ptrs_cpu_;
  void **dst_rgbx_ptrs_mlu_;

  std::string estr_;
};  // MluReszieRgbxPrivate

extern uint32_t getPixFmtChannelNum(cnPixelFormat pixfmt);
extern uint32_t getSizeOfDepth(cnDepth_t depth);

int genConvFilterAndBias(float *bias,
                         int16_t *filter,
                         cnPixelFormat src_pixfmt,
                         cnPixelFormat dst_pixfmt,
                         cnColorSpace color_space,
                         uint32_t dst_chn,
                         uint32_t ci) {
  /* get position of u, v, r, g, b */
  int offset_u = 0, offset_v = 0;
  int r_idx = 0, g_idx = 0, b_idx = 0;
  if (CN_PIX_FMT_NV12 == src_pixfmt) {
    offset_u = 0;
    offset_v = 1;
  } else {
    offset_u = 1;
    offset_v = 0;
  }

  switch (dst_pixfmt) {
    case CN_PIX_FMT_RGB:
    case CN_PIX_FMT_RGBA:
      r_idx = 0;
      g_idx = 1;
      b_idx = 2;
      break;
    case CN_PIX_FMT_BGR:
    case CN_PIX_FMT_BGRA:
      r_idx = 2;
      g_idx = 1;
      b_idx = 0;
      break;
    case CN_PIX_FMT_ARGB:
      r_idx = 1;
      g_idx = 2;
      b_idx = 3;
      break;
    case CN_PIX_FMT_ABGR:
      r_idx = 3;
      g_idx = 2;
      b_idx = 1;
      break;
    default:
      return -1;
  }

  /* prepare coefficient (quantified data, position = -13) */
  int16_t rv, gu, gv, bu;
  float r_bias, g_bias, b_bias;
  if (color_space == CN_COLOR_SPACE_BT_601) {
    rv = 0x3312;
    r_bias = -222.912;
    gu = 0xF375;
    gv = 0xE5FC;
    g_bias = 135.616;
    bu = 0x408B;
    b_bias = -276.8;
  } else if (color_space == CN_COLOR_SPACE_BT_709) {
    rv = 0x3958;
    r_bias = -248.0;
    gu = 0xF92F;
    gv = 0xEEE9;
    g_bias = 76.992;
    bu = 0x43A6;
    b_bias = -289.216;
  } else {
    return -1;
  }

  /******* filter ********/
  uint32_t offset_r;
  uint32_t offset_g;
  uint32_t offset_b;
  uint32_t offset_ry, offset_rv;
  uint32_t offset_gy, offset_gu, offset_gv;
  uint32_t offset_by, offset_bu;

  uint32_t co = ci * dst_chn;
  for (uint32_t y_idx = 0; y_idx < ci; ++y_idx) {
    offset_r = y_idx * dst_chn * dst_chn + r_idx * dst_chn;
    offset_ry = (offset_r % co + offset_r / co) * ci * 2 + y_idx;
    offset_rv = offset_ry + ci - y_idx % 2 + offset_v;

    offset_g = y_idx * dst_chn * dst_chn + g_idx * dst_chn;
    offset_gy = (offset_g % co + offset_g / co) * ci * 2 + y_idx;
    offset_gu = offset_gy + ci - y_idx % 2 + offset_u;
    offset_gv = offset_gy + ci - y_idx % 2 + offset_v;

    offset_b = y_idx * dst_chn * dst_chn + b_idx * dst_chn;
    offset_by = (offset_b % co + offset_b / co) * ci * 2 + y_idx;
    offset_bu = offset_by + ci - y_idx % 2 + offset_u;

    filter[offset_ry] = 0x253F;
    filter[offset_rv] = rv;
    filter[offset_gy] = 0x253F;
    filter[offset_gu] = gu;
    filter[offset_gv] = gv;
    filter[offset_by] = 0x253F;
    filter[offset_bu] = bu;
  }  // for (int y_idx = 0; y_idx < ci_; ++y_idx)

  /******* bias ********/
  if (dst_chn == 3) {
    for (int i = 0; i < 64; ++i) {
      bias[i * 3 + r_idx] = r_bias;
      bias[i * 3 + g_idx] = g_bias;
      bias[i * 3 + b_idx] = b_bias;
    }
  } else {
    for (int i = 0; i < 16; ++i) {
      bias[i * 4 + r_idx] = r_bias;
      bias[i * 4 + g_idx] = g_bias;
      bias[i * 4 + b_idx] = b_bias;
    }
  }  // if (dst_chn == 3)

  return 0;
}

void getBiasAndWeight(MluYUV2RGBPrivate *d_ptr_) {
  /* worksapce = weight + bias */
  int ret;
  cnrtRet_t cnret;
  size_t workspace_size;
  uint32_t dst_chn = d_ptr_->dst_pix_chn_num;
  uint32_t ci = CI;
  uint32_t co = ci * dst_chn;
  uint32_t bias_len = co;
  uint32_t weight_len = 2 * ci * co;
  if (dst_chn == 4) {
    bias_len = 64;
  }
  workspace_size = bias_len * sizeof(float) + weight_len * sizeof(int16_t);
  cnret = cnrtMalloc((void **)&d_ptr_->workspace, workspace_size);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "cnrtMalloc failed ..." << std::endl;
    return;
  }

  /* get bias and filter addr */
  float *bias = reinterpret_cast<float *>(malloc(bias_len * sizeof(float) + weight_len * sizeof(int16_t)));
  memset(bias, 0, bias_len * sizeof(float) + weight_len * sizeof(int16_t));

  int16_t *filter = (int16_t *)bias + bias_len * 2;
  ret = genConvFilterAndBias(bias, filter,
                       d_ptr_->src_pixfmt,
                       d_ptr_->dst_pixfmt,
                       d_ptr_->color_space,
                       dst_chn, ci);
  if (ret < 0) {
    std::cout << "genConvFilterAndBias failed ... " << std::endl;
    return;
  }

  d_ptr_->bias_mlu = reinterpret_cast<float *>(d_ptr_->workspace);
  d_ptr_->filter_mlu = reinterpret_cast<int16_t *>(d_ptr_->bias_mlu)
                       + bias_len * 2;

  cnret = cnrtMemcpy(d_ptr_->workspace, bias,
             bias_len * sizeof(float) + 2 * ci * co * sizeof(int16_t),
             CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "cnrtMemcpy failed ..." << std::endl;
    return;
  }
  free(bias);
}

int mluop_convert_yuv2rgb_invoke_init(HANDLE *h,
                                      int width,
                                      int height,
                                      cnPixelFormat src_pix_fmt,
                                      cnPixelFormat dst_pix_fmt,
                                      cnDepth_t depth) {
  MluYUV2RGBPrivate *d_ptr_ = new MluYUV2RGBPrivate;
  cnrtCreateQueue(&d_ptr_->queue_);
  cnrtRet_t cnret;
  d_ptr_->depth = depth;
  d_ptr_->input_h = height;
  d_ptr_->input_w = width;
  d_ptr_->output_h = height;
  d_ptr_->output_w = width;
  d_ptr_->depth = depth;
  d_ptr_->src_pixfmt = src_pix_fmt;
  d_ptr_->dst_pixfmt = dst_pix_fmt;
  d_ptr_->color_space = CN_COLOR_SPACE_BT_601;

  d_ptr_->depth_size = getSizeOfDepth(depth);
  d_ptr_->dst_pix_chn_num = getPixFmtChannelNum(d_ptr_->dst_pixfmt);
  d_ptr_->input_y_stride_in_bytes = d_ptr_->input_w * d_ptr_->depth_size;
  d_ptr_->input_uv_stride_in_bytes = d_ptr_->input_w * d_ptr_->depth_size;
  d_ptr_->output_stride_in_bytes = d_ptr_->output_w * d_ptr_->dst_pix_chn_num
                                   * d_ptr_->depth_size;

  d_ptr_->src_yuv_ptrs_cpu_ = (void **)malloc(d_ptr_->batch_size * 2 *
                                              sizeof(void *));
  d_ptr_->dst_rgbx_ptrs_cpu_ = (void **)malloc(d_ptr_->batch_size *
                                               sizeof(void*));

  cnret = cnrtMalloc((void **)&d_ptr_->src_yuv_ptrs_mlu_,
                     d_ptr_->batch_size * 2 * sizeof(void*));
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtMalloc((void **)&d_ptr_->dst_rgbx_ptrs_mlu_,
                     d_ptr_->batch_size * sizeof(void*));
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }

  getBiasAndWeight(d_ptr_);

  d_ptr_->kparam_ = new ConvertYUV2RGBXKernelParam;
  d_ptr_->kparam_->conv_kernel_gdram = d_ptr_->filter_mlu;
  d_ptr_->kparam_->bias_gdram = d_ptr_->bias_mlu;
  d_ptr_->kparam_-> height= height;
  d_ptr_->kparam_-> width= width;
  d_ptr_->kparam_-> src_y_stride= d_ptr_->input_y_stride_in_bytes;
  d_ptr_->kparam_-> src_uv_stride= d_ptr_->input_uv_stride_in_bytes;
  d_ptr_->kparam_-> dst_stride= d_ptr_->output_stride_in_bytes;
  d_ptr_->kparam_->batch_size = d_ptr_->batch_size;
  d_ptr_->kparam_->ci = CI;
  d_ptr_->kparam_->out_chn = d_ptr_->dst_pix_chn_num;

  cnret = cnrtCreateKernelInitParam(&d_ptr_->kparam_->init_param);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "cnrtCreateKernelInitParam failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtInitKernelMemory(
          reinterpret_cast<void*>(&MLUUnion1KernelYuv420spToRgb),
          d_ptr_->kparam_->init_param);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "cnrtInitKernelMemory failed. Error code:" << std::endl;
    return -1;
  }

  *h = static_cast<void *>(d_ptr_);
  return 0;
}

int mluop_convert_yuv2rgb_invoke_exec(HANDLE h,
                                      void *input_y, void *input_uv,
                                      void *output) {
  MluYUV2RGBPrivate *d_ptr_ = static_cast<MluYUV2RGBPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     std::cout << "cnrt queue is nlll." << std::endl;
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

  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->src_yuv_ptrs_mlu_, d_ptr_->src_yuv_ptrs_cpu_,
                     d_ptr_->batch_size * 2 * sizeof(void *),
                     CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return -1;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_rgbx_ptrs_mlu_, d_ptr_->dst_rgbx_ptrs_cpu_,
                     sizeof(void*) * d_ptr_->batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Memcpy host to device failed. Error code: " << std::endl;
    return -1;
  }

  float r_ret = invokeConvertYUV2RGBXKernel(
                reinterpret_cast<uint8_t **>(d_ptr_->src_yuv_ptrs_mlu_),
                reinterpret_cast<uint8_t **>(d_ptr_->dst_rgbx_ptrs_mlu_),
                d_ptr_->kparam_, d_ptr_->ftype_, d_ptr_->dim_,
                d_ptr_->queue_, &d_ptr_->estr_);

  return r_ret;
}

int mluop_convert_yuv2rgb_invoke_destroy(HANDLE h) {
  MluYUV2RGBPrivate *d_ptr_ = static_cast<MluYUV2RGBPrivate *>(h);

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
    if (d_ptr_->src_yuv_ptrs_cpu_) {
      free(d_ptr_->src_yuv_ptrs_cpu_);
      d_ptr_->src_yuv_ptrs_cpu_ = nullptr;
    }
    if (d_ptr_->src_yuv_ptrs_mlu_) {
      cnrtFree(d_ptr_->src_yuv_ptrs_mlu_);
      d_ptr_->src_yuv_ptrs_mlu_ = nullptr;
    }
    d_ptr_->src_yuv_ptrs_cache_.clear();

    if (d_ptr_->dst_rgbx_ptrs_cpu_) {
      free(d_ptr_->dst_rgbx_ptrs_cpu_);
      d_ptr_->dst_rgbx_ptrs_cpu_ = nullptr;
    }
    if (d_ptr_->dst_rgbx_ptrs_mlu_) {
      cnrtFree(d_ptr_->dst_rgbx_ptrs_mlu_);
      d_ptr_->dst_rgbx_ptrs_mlu_ = nullptr;
    }
    if (d_ptr_->workspace) {
      cnrtFree(d_ptr_->workspace);
      d_ptr_->workspace = nullptr;
    }
    d_ptr_->dst_rgbx_ptrs_cache_.clear();

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
