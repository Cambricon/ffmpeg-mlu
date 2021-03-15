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

#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <fstream>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <deque>
#include "cnrt.h"
#include "string.h"
#include "kernel.h"
#include "mluop.h"

#define CI 64
using namespace::std;

#define PRINT_TIME 0

#define CNRT_ERROR_CHECK(ret)                                                  \
  if (ret != CNRT_RET_SUCCESS) {                                               \
    fprintf(stderr, "error occur, func: %s, line: %d\n", __func__, __LINE__);  \
    return 0;                                                                 \
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


extern bool cncvYuv420spToRgbx_Batch_8u(void **src,
                                        void **dst,
                                        int16_t *filter_mlu,
                                        float *bias_mlu,
                                        int height,
                                        int width,
                                        int y_stride,
                                        int uv_stride,
                                        int rgb_stride,
                                        uint32_t batch_num,
                                        uint32_t ci,
                                        uint32_t dst_chn,
                                        cnrtDim3_t dim_,
                                        cnrtFunctionType_t ftype_,
                                        cnrtQueue_t queue_);
                                        
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

static cnPixelFormat getPixFmtFromChannelNum(uint32_t chan_num) {
  switch (chan_num) {
  case 0:
    return CN_PIX_FMT_NV12;
    break;
  case 1:
    return CN_PIX_FMT_NV21;
    break;
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
    std::cout << "unsupport pix fmt, defaule rgb ..." << std::endl;
    return CN_PIX_FMT_RGB;
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

int genConvFilterAndBias(float *bias,
                         int16_t *filter,
                         cnPixelFormat src_pixfmt,
                         cnPixelFormat dst_pixfmt,
                         cnColorSpace color_space,
                         uint32_t dst_chn,
                         uint32_t ci) {
  /* get position of u, v, r, g, b */
  int offset_u, offset_v, r_idx, g_idx, b_idx;
  auto parseInputAndOutputType = [&]() {
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
    //return 0;
  };  // auto parseInputAndOutputType = [&]()
  parseInputAndOutputType();

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
  int offset_r;
  int offset_g;
  int offset_b;
  int offset_ry, offset_rv;
  int offset_gy, offset_gu, offset_gv;
  int offset_by, offset_bu;

  uint32_t co = ci * dst_chn;
  for (int y_idx = 0; y_idx < ci; ++y_idx) {
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
  uint32_t dst_elem_size = d_ptr_->output_h * d_ptr_->output_stride_in_bytes;
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
  cnrtMalloc((void **)&d_ptr_->workspace, workspace_size);
  
  /* get bias and filter addr */
  float *bias = reinterpret_cast<float *>
        (malloc(bias_len * sizeof(float) + 2 * ci * co * sizeof(int16_t)));
  memset(bias, 0, bias_len * sizeof(float) + 2 * ci * co * sizeof(int16_t));
  int16_t *filter = (int16_t *)bias + bias_len * 2;
  genConvFilterAndBias(bias, filter, d_ptr_->src_pixfmt, d_ptr_->dst_pixfmt, d_ptr_->color_space, dst_chn, ci);
  d_ptr_->bias_mlu = reinterpret_cast<float *>(d_ptr_->workspace);
  d_ptr_->filter_mlu = reinterpret_cast<int16_t *>(d_ptr_->bias_mlu) + bias_len * 2;
  CNRT_CHECK(cnrtMemcpy(d_ptr_->workspace, bias, bias_len * sizeof(float) + 2 * ci * co * sizeof(int16_t),
                        CNRT_MEM_TRANS_DIR_HOST2DEV));
  free(bias);
}

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
  d_ptr_->output_stride_in_bytes = d_ptr_->output_w * d_ptr_->dst_pix_chn_num * d_ptr_->depth_size;

  d_ptr_->src_yuv_ptrs_cpu_ = (void **)malloc(d_ptr_->batch_size * 2 * sizeof(void *));
  d_ptr_->dst_rgbx_ptrs_cpu_ = (void **)malloc(d_ptr_->batch_size * sizeof(void*));

  cnret = cnrtMalloc((void **)&d_ptr_->src_yuv_ptrs_mlu_, d_ptr_->batch_size * 2 * sizeof(void*));
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }
  cnret = cnrtMalloc((void **)&d_ptr_->dst_rgbx_ptrs_mlu_, d_ptr_->batch_size * sizeof(void*));
  if (cnret != CNRT_RET_SUCCESS) {
    std::cout << "Malloc mlu buffer failed. Error code:" << std::endl;
    return -1;
  }

  getBiasAndWeight(d_ptr_);

  *h = static_cast<void *>(d_ptr_);
  return 0;
}

int mluop_convert_yuv2rgb_invoke_exec(HANDLE h,
                                      void *input_y, void *input_uv, void *output) {
  MluYUV2RGBPrivate *d_ptr_ = static_cast<MluYUV2RGBPrivate *>(h);
  if (nullptr == d_ptr_->queue_) {
     std::cout << "cnrt queue is nlll." << std::endl;
     return -1;
  }
  uint32_t y_size = d_ptr_->input_h * d_ptr_->input_y_stride_in_bytes;
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
          d_ptr_->batch_size * 2 * sizeof(void *), CNRT_MEM_TRANS_DIR_HOST2DEV);
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
  cnrtPlaceNotifier(event_begin, d_ptr_->queue_);
  #endif
  bool ret = cncvYuv420spToRgbx_Batch_8u(d_ptr_->src_yuv_ptrs_mlu_,
                                         d_ptr_->dst_rgbx_ptrs_mlu_,
                                         d_ptr_->filter_mlu,
                                         d_ptr_->bias_mlu,
                                         d_ptr_->input_h,
                                         d_ptr_->input_w,
                                         d_ptr_->input_y_stride_in_bytes,
                                         d_ptr_->input_uv_stride_in_bytes,
                                         d_ptr_->output_stride_in_bytes,
                                         d_ptr_->batch_size,
                                         CI,
                                         d_ptr_->dst_pix_chn_num,
                                         d_ptr_->dim_,
                                         d_ptr_->ftype_,
                                         d_ptr_->queue_);
  #if PRINT_TIME
  cnrtPlaceNotifier(event_end, d_ptr_->queue_);
  if (CNRT_RET_SUCCESS != cnrtSyncQueue(d_ptr_->queue_)) {
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
  CNRT_CHECK(cnrtSyncQueue(d_ptr_->queue_));
  #endif

  return 0;
}

int mluop_convert_yuv2rgb_invoke_destroy(HANDLE h) {
  MluYUV2RGBPrivate *d_ptr_ = static_cast<MluYUV2RGBPrivate *>(h);

  if (d_ptr_) {
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

/*----------------------------------------------------------------*/
void *process_convert_invoke_yuv2rgbx(void) {
  bool save_flag = 1;
  int batch_size = 1;
  uint32_t width =1920;
  uint32_t height = 1080;
  uint32_t frame_num = 1;

  uint32_t device_id = 0;
  uint32_t depth_size = 1; // depth_size: 1->uint8, 2->f16, 4->f32
  uint32_t src_pix_fmt_idx = 0; // pix_chn_num: 3->rgb/rgb, 4->rgba/argb/...
  uint32_t src_pix_chn_num = 1; // rgb
  uint32_t dst_pix_fmt_idx = 8; // pix_chn_num: 3->rgb/rgb, 4->rgba/argb/.../////////
  uint32_t dst_pix_chn_num = 3; // rgb

  const char *input_file = "../../data/images/1920_1080_nv12_1.yuv";
  const char *output_file = "out.rgb";

  HANDLE handle;
  set_cnrt_ctx(device_id, CNRT_CHANNEL_TYPE_NONE);
  /*-------------------init op------------------*/
  mluop_convert_yuv2rgb_invoke_init(&handle,
                                    width,
                                    height,
                                    getPixFmtFromChannelNum(src_pix_fmt_idx),
                                    getPixFmtFromChannelNum(dst_pix_fmt_idx),
                                    getSizeFromDepth(depth_size));
  #if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[init] time: %.3f ms\n", time_use/1000);
  #endif
  /*----------------------prepare_input_data-------------------------*/
  //uint32_t y_elem_size = height * width * depth_size * src_pix_chn_num;
  uint32_t src_y_elem_size = height * width * depth_size * src_pix_chn_num;
  uint32_t src_uv_elem_size = height / 2 * width * depth_size * src_pix_chn_num;
  uint32_t src_elem_size = src_y_elem_size + src_uv_elem_size;
  uint32_t dst_elem_size = height * width * depth_size * dst_pix_chn_num;

  //read image to src_cpu
  uint8_t *src_cpu = (uint8_t *)malloc(src_elem_size);
  uint8_t *dst_cpu = (uint8_t *)malloc(dst_elem_size);
  // for (int i = 0; i < batch_size; i ++) {
  //    dst_cpu[i] = (uint8_t *)malloc(dst_elem_size);
  // }
    FILE *fp = fopen(input_file, "rb");
    if (fp == NULL) {
      printf("Error opening input image for write \n");
    }
    uint32_t ss = ftell(fp);
    size_t wt_;
    wt_ = fread(src_cpu, 1, src_elem_size, fp);
    if (wt_ != src_elem_size) {
      cout << "write data fail" << endl;
    }
    fclose(fp);
    fp = NULL;
    void *src_y_mlu;
    void *src_uv_mlu;
    void *dst_mlu;
    cnrtMalloc((void **)(&src_y_mlu), src_y_elem_size);
    cnrtMalloc((void **)(&src_uv_mlu), src_uv_elem_size);
    cnrtMemcpy(src_y_mlu, src_cpu, src_y_elem_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
    cnrtMemcpy(src_uv_mlu, (src_cpu + src_y_elem_size), src_uv_elem_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
    cnrtMalloc((void **)(&dst_mlu), dst_elem_size);

  #if PRINT_TIME
  gettimeofday(&start, NULL);
  #endif
  /*-------execute op-------*/
  for (uint32_t i = 0; i < frame_num; i++) {
    mluop_convert_yuv2rgb_invoke_exec(handle, src_y_mlu, src_uv_mlu, dst_mlu);
  }

  cnrtMemcpy(dst_cpu, dst_mlu, dst_elem_size, CNRT_MEM_TRANS_DIR_DEV2HOST);
  #if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[exec] time(ave.): %.3f ms, total frame: %d\n", (time_use/1000.0)/frame_num, frame_num);
  #endif
  /*-----------------------save file----------------------*/
  if (save_flag){
      //prepare_output_data(handle, output_file);
    FILE *fp = fopen(output_file, "wb");
    if (fp == NULL) {
      printf("Error opening output image for write \n");
    }
    size_t written_;
    // host: dump result to file
      written_ = fwrite(dst_cpu, 1, dst_elem_size, fp);
      if (written_ != dst_elem_size) {
        printf("Error writting rgb data \n");
      }
    fclose(fp);
    fp = NULL;
  }
  #if PRINT_TIME
  gettimeofday(&start, NULL);
  #endif
  /*-------destroy op-------*/
  mluop_convert_yuv2rgb_invoke_destroy(handle);

  #if PRINT_TIME  
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[destroy] time: %.3f ms\n", time_use/1000);
  #endif

  if (src_cpu)
    free(src_cpu);
  if (src_y_mlu)
    cnrtFree(src_y_mlu);
  if (src_uv_mlu)
    cnrtFree(src_uv_mlu);
  if (dst_cpu)
    free(dst_cpu);
  if (dst_mlu)
    cnrtFree(dst_mlu);
  return NULL;
}

int main(int argc, char **argv) {
  // cnrt: init
  CNRT_CHECK(cnrtInit(0));
  process_convert_invoke_yuv2rgbx();
  cnrtDestroy();
  return 0;
}
