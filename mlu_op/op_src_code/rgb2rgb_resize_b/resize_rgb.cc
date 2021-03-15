
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
// #include <chrono>
#include <ctime>
#include <deque>
#include <iostream>
#include <string>
#include <memory>
#include <utility>
#include <sys/time.h>

#include "cnrt.h"
#include "resize_rgb_mlu.h"

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

typedef void *HANDLE;

typedef enum {
  CN_PIX_FMT_NONE = -1,
  CN_PIX_FMT_NV12 = 0,
  CN_PIX_FMT_NV21 = 1,
  CN_PIX_FMT_I420 = 2,
  CN_PIX_FMT_YUYV = 3,
  CN_PIX_FMT_UYVY = 4,
  CN_PIX_FMT_YVYU = 5,
  CN_PIX_FMT_VYUY = 6,
  CN_PIX_FMT_P010 = 7,
  CN_PIX_FMT_RGB  = 8,
  CN_PIX_FMT_BGR  = 9,
  CN_PIX_FMT_ARGB =10,
  CN_PIX_FMT_ABGR =11,
  CN_PIX_FMT_RGBA =12,
  CN_PIX_FMT_BGRA =13,
  CN_PIX_FMT_GRAY =14,
}cnPixelFormat;

typedef enum {
  CN_DEPTH_NONE = -1,
  CN_DEPTH_8U  = 0,
  CN_DEPTH_8S  = 1,
  CN_DEPTH_16U  = 2,
  CN_DEPTH_16S  = 3,
  CN_DEPTH_32U  = 4,
  CN_DEPTH_32S  = 5,
  CN_DEPTH_16F  = 6,
  CN_DEPTH_32F  = 7,
}cnDepth_t;

typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;
}cnRoi;

/*------------------------resize rgbx2rgbx invoke----------------------*/
int mluop_resize_rgbx_invoke_init_v(HANDLE *h,
                                  int input_w, int input_h, int output_w, int output_h,
                                  cnPixelFormat pix_fmt, cnDepth_t depth);

int mluop_resize_rgbx_invoke_exec_v(HANDLE h, void *input, void *src_rois_mlu, void *output,
                                 uint32_t d_x, uint32_t d_y, uint32_t d_w, uint32_t d_h);
int mluop_resize_rgbx_invoke_destroy_v(HANDLE h);

static int set_cnrt_ctx(unsigned int device_id, cnrtChannelType_t channel_id) {
  cnrtDev_t dev;
  cnrtRet_t ret;
  ret = cnrtGetDeviceHandle(&dev, device_id);
  CNRT_ERROR_CHECK(ret);
  ret = cnrtSetCurrentDevice(dev);
  CNRT_ERROR_CHECK(ret);
  if (channel_id >= CNRT_CHANNEL_TYPE_0) {
    ret = cnrtSetCurrentChannel(channel_id);
    CNRT_ERROR_CHECK(ret);
  }
  return 0;
}

uint32_t getPixFmtChannelNum(cnPixelFormat pixfmt) {
  if (pixfmt == CN_PIX_FMT_BGR || pixfmt == CN_PIX_FMT_RGB) {
    return 3;
  } else if (pixfmt == CN_PIX_FMT_ABGR || pixfmt == CN_PIX_FMT_ARGB ||
             pixfmt == CN_PIX_FMT_BGRA || pixfmt == CN_PIX_FMT_RGBA) {
    return 4;
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
  return 0;
}

cnPixelFormat getPixFmtFromChannelNum(uint32_t chan_num) {
  switch (chan_num) {
  case 8:
    return CN_PIX_FMT_RGB;
    break;
  case 9:
    return CN_PIX_FMT_BGR;
    break;
  case 10:
    return CN_PIX_FMT_ARGB;
    break;
  case 11:
    return CN_PIX_FMT_ABGR;
    break;
  case 12:
    return CN_PIX_FMT_RGBA;
    break;
  case 13:
    return CN_PIX_FMT_BGRA;
    break;
  default:
    std::cout << "unsupport pix fmt ...";
    return CN_PIX_FMT_NONE;
    break;
  }
}

cnDepth_t getSizeFromDepth(uint32_t depth) {
  if (1 == depth) {
    return CN_DEPTH_8U;
  } else if (2 == depth) {
    return CN_DEPTH_16F;
  } else if (4 == depth) {
    return CN_DEPTH_32F;
  } else {
    std::cout << "unsupport data depth, defalut unit8" << std::endl;
    return CN_DEPTH_8U;
  }
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

  std::string estr_;
};

int mluop_resize_rgbx_invoke_init_v(HANDLE *h,
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

int mluop_resize_rgbx_invoke_exec_v(HANDLE h,
                                 void *input,
                                 void *src_rois_mlu,
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
                reinterpret_cast<uint32_t *>(src_rois_mlu),
                static_cast<void **>(d_ptr_->dst_rgbx_ptrs_mlu_),
                d_x, d_y, d_w, d_h,
                d_ptr_->kparam_, d_ptr_->ftype_, d_ptr_->dim_,
                d_ptr_->queue_, &d_ptr_->estr_);

  return r_ret;
}

int mluop_resize_rgbx_invoke_destroy_v(HANDLE h) {
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

/*----------------------------------------------------------------*/
void *process_resize_invoke_rgbx(void) {
  bool save_flag = 1;
  int batch_size = 1;
  uint32_t input_w =1920;
  uint32_t input_h = 1080;
  uint32_t dst_w = 1280;
  uint32_t dst_h = 720;

  uint32_t depth_size = 1; // depth_size: 1->uint8, 2->f16, 4->f32
  uint32_t pix_chn_num = 3; // pix_chn_num: 3->rgb/rgb, 4->rgba/argb/...
  uint32_t pix_fmt_idx = 8;
  uint32_t device_id = 0;

  const char *filename = "input.rgb";
  const char *output_file = "out.rgb";

  HANDLE handle;

  set_cnrt_ctx(device_id, CNRT_CHANNEL_TYPE_NONE);
  /*-------------------init op------------------*/
  mluop_resize_rgbx_invoke_init_v(&handle,
                                input_w,
                                input_h,
                                dst_w,
                                dst_h,
                                getPixFmtFromChannelNum(pix_fmt_idx),
                                getSizeFromDepth(depth_size));
  // prepare roi data, roi(x, y, w, h)
  uint32_t *src_rois_cpu = (uint32_t *)malloc(batch_size * 4 * sizeof(uint32_t));
  uint32_t *src_rois_mlu = nullptr;
  CNRT_CHECK(cnrtMalloc((void **)(&src_rois_mlu), batch_size * 4 * sizeof(uint32_t)));
  for (uint32_t i = 0; i < batch_size; i ++) {
    src_rois_cpu[i * 4 + 0] = 0;
    src_rois_cpu[i * 4 + 1] = 0;
    src_rois_cpu[i * 4 + 2] = input_w;
    src_rois_cpu[i * 4 + 3] = input_h;
  }
  CNRT_CHECK(cnrtMemcpy(src_rois_mlu, src_rois_cpu, batch_size * 4 * sizeof(uint32_t),
                        CNRT_MEM_TRANS_DIR_HOST2DEV));

  // prepare src size
  uint32_t src_stride = input_w * pix_chn_num * depth_size;
  uint32_t src_size = input_h * src_stride;
  void *src_cpu = (void *)malloc(src_size);

  uint32_t dst_stride = dst_w * pix_chn_num * depth_size;
  uint32_t dst_size = dst_h * dst_stride;
  void *dst_cpu = (void *)malloc(dst_size);

  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    printf("Error opening input image for write \n");
    return NULL;
  }
  uint32_t ss = ftell(fp);
  size_t wt_;
  wt_ = fread(src_cpu, 1, src_size, fp);
  if (wt_ != src_size) {
    // printf("Error reading rgb data, file size: %d, wt size: %d, dst size: %d \n", ss, wt_, src_size);
    return NULL;
  }
  fclose(fp);
  fp = NULL;

  void *src_mlu;
  void *dst_mlu;
  CNRT_CHECK(cnrtMalloc((void **)(&src_mlu), depth_size * src_size));
  CNRT_CHECK(cnrtMemcpy(src_mlu, src_cpu, src_size, CNRT_MEM_TRANS_DIR_HOST2DEV));
  CNRT_CHECK(cnrtMalloc((void **)(&dst_mlu), depth_size * dst_size));

  uint32_t d_x = 0, d_y = 0, d_w = dst_w, d_h = dst_h;
  mluop_resize_rgbx_invoke_exec_v(handle, src_mlu, src_rois_mlu, dst_mlu, d_x, d_y, d_w, d_h);

  CNRT_CHECK(cnrtMemcpy(dst_cpu, dst_mlu, dst_size, CNRT_MEM_TRANS_DIR_DEV2HOST));

  /*-------destroy op-------*/
  mluop_resize_rgbx_invoke_destroy_v(handle);
  /*-------sace file-------*/
  if (save_flag) {
    FILE *fp = fopen(output_file, "wb");
    if (fp == NULL) {
      printf("Error opening output image for write \n");
      return NULL;
    }
    size_t written_;
    written_ = fwrite(dst_cpu, 1, dst_size, fp);
    if (written_ != dst_size) {
      printf("Error writting rgb data \n");
      return NULL;
    }
    fclose(fp);
    fp = NULL;
  }

  if (src_rois_cpu)
    free(src_rois_cpu);
  if (src_rois_mlu)
    cnrtFree(src_rois_mlu);
  if (src_cpu)
    free(src_cpu);
  if (src_mlu)
    cnrtFree(src_mlu);
  if (dst_cpu)
    free(dst_cpu);
  if (dst_mlu)
    cnrtFree(dst_mlu);
  return NULL;
}

int main(int argc, char **argv) {
  // cnrt: init
  CNRT_CHECK(cnrtInit(0));
  process_resize_invoke_rgbx();
  cnrtDestroy();
  return 0;
}
