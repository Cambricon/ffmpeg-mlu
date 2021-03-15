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

#include <cnrt.h>
#include <sys/time.h>
#include <ctime>
#include <iostream>
#include <string>
#include "ResizeAndConvertKernel.h"
#include "ResizeAndConvertMacro.h"
#include "half.hpp"

using std::string;
using std::to_string;
struct KernelParam {
  half* consts_mlu = nullptr;
  // half* maskUV_mlu = nullptr;
  half* yuvFilter = nullptr;
  half* yuvBias = nullptr;
  int s_row, s_col, d_row, d_col;
  int roi_x, roi_y, roi_w, roi_h;
  int channelIn, channelOut, layerIn, layerOut;
  int input2half, output2uint;
  int scaleX, scaleY, batchNum;
  uint32_t* cycles = nullptr;
  cnrtKernelInitParam_t func_init_param = nullptr;
};

void FreeKernelParam(KernelParam* param) {
  if (param) {
    if (param->consts_mlu) {
      cnrtFree(param->consts_mlu);
    }
    if (param->cycles) {
      cnrtFree(param->cycles);
    }
    //   if (param->maskUV_mlu) {
    //     cnrtFree(param->maskUV_mlu);
    //   }
    if (param->func_init_param) {
      cnrtDestroyKernelInitParamAndMemory(param->func_init_param);
    }
    delete param;
  }
}

int PrepareKernelParam(int s_row, int s_col, int d_row, int d_col, int roi_x, int roi_y, int roi_w, int roi_h,
                       int color_mode, int data_type, int batchsize, KernelParam** param, string* estr) {
  *param = new KernelParam;
  // parse mode
  int inputType, outputType;

  switch (color_mode) {
    case YUV_TO_RGBA_NV12:
      inputType = YUVNV12;
      outputType = RGBA;
      break;
    case YUV_TO_RGBA_NV21:
      inputType = YUVNV21;
      outputType = RGBA;
      break;
    case YUV_TO_BGRA_NV12:
      inputType = YUVNV12;
      outputType = BGRA;
      break;
    case YUV_TO_BGRA_NV21:
      inputType = YUVNV21;
      outputType = BGRA;
      break;
    case YUV_TO_ARGB_NV12:
      inputType = YUVNV12;
      outputType = ARGB;
      break;
    case YUV_TO_ARGB_NV21:
      inputType = YUVNV21;
      outputType = ARGB;
      break;
    case YUV_TO_ABGR_NV12:
      inputType = YUVNV12;
      outputType = ABGR;
      break;
    case YUV_TO_ABGR_NV21:
      inputType = YUVNV21;
      outputType = ABGR;
      break;
    case RGBA_TO_RGBA:
      inputType = RGBA;
      outputType = RGBA;
      break;
    default:
      std::cout << "COLOR CONVERSION NOT SURPPORTED!" << std::endl;
      assert(0);
      return -1;
  }

  // parse inputType
  int channelIn = 1;   // ch in NCHW mode
  int channelOut = 1;  // ch in NCHW mode
  int layerIn = 1;     // ch in NHWC mode
  int layerOut = 1;    // ch in HHWC mode
  int reverseChannel = 0;
  int input2half = 0;
  int output2uint = 0;

  int scaleX = (roi_w << 16) / (d_col);
  int scaleY = (roi_h << 16) / (d_row);

  if (inputType == YUVNV21) {
    inputType = YUVNV12;
    reverseChannel = true;
  }
  switch (inputType) {
    case RGB:
      channelIn = 3;
      break;
    case RGBA:
      channelIn = 4;
      break;
    case GRAY:
      channelIn = 1;
      break;
    case YUVNV12:
      channelIn = 1;
      layerIn = 3;
      break;
    default:
      std::cout << "INPUT COLOR_TYPE NOT SURPPORTED!" << std::endl;
      assert(0);
      return -1;
  }

  // parse outputType
  switch (outputType) {
    case RGB:
    case BGR:
      channelOut = 3;
      break;
    case RGBA:
    case BGRA:
    case ARGB:
    case ABGR:
      channelOut = 4;
      break;
    case GRAY:
      channelOut = 1;
      break;
    default:
      std::cout << "OUTPUT COLOR_TYPE NOT SURPPORTED!" << std::endl;
      assert(0);
      return -1;
  }

  // input2half = 1 when in_datatype = uint8
  input2half = 1 - sizeof(IN_DATA_TYPE) / 2;
  // output2uint = 1 when out_datatype = uint8
  output2uint = 1 - sizeof(OUT_DATA_TYPE) / 2;
  half* consts = (half*)malloc((2 * CI * CO + CO) * sizeof(int16_t));
  // int ratio = 150;
  // int total = ratio * (ratio + 1) / 2;
  // half* maskTP = (half*)malloc((CI * CI * total) * sizeof(int16_t));
  // half* maskUV = (half*)malloc((CI * CI * total) * sizeof(int16_t));
  // half temp[CI * CI];
  // for (int i = 0; i < CI; i++) {
  //  for (int j = 0; j < CI; j++) {
  //    temp[j + CI * i] = 0;
  //  }
  //}
  // for (int i = 0; i < CI; i++) {
  //  temp[i * CI + i] = 1;
  //}
  // for (int i = 0; i < CI * total; i++) {
  //  for (int j = 0; j < CI; j++) {
  //    maskUV[j + CI * i] = 0;
  //    maskTP[j + CI * i] = 0;
  //  }
  //}

  // int multOffset = 0;
  // int tSize = CI * 4;
  // for (int mult = 1; mult <= ratio; mult++) {
  //  multOffset += CI * CI * (mult - 1);
  //  for (int i = 0; i < CI / 4; i++) {
  //    for (int j = 0; j < mult; j++) {
  //      memcpy(maskTP + multOffset + tSize * (i * mult + j),
  //               temp + tSize * i,
  //               tSize * sizeof(int16_t));
  //    }
  //  }

  //  int kernelNum = CI * mult / LT_NUM;
  //  int ltSize = CI * mult / LT_NUM * CI;
  //  for (int lt = 0; lt < LT_NUM; lt++) {
  //    for (int kernel = 0; kernel < kernelNum; kernel++) {
  //      memcpy(maskUV + multOffset + lt * ltSize + kernel * CI,
  //             maskTP + multOffset + kernel * LT_NUM * CI + lt * CI,
  //             CI * sizeof(int16_t));
  //    }
  //  }
  //}
  // for (int i = 0; i < 64; i++) {
  //  for (int j = 0; j < 64; j++) {
  //    maskUV[j + 64 * i] = 0;
  //  }
  //}
  // for (int i = 0; i < 64; i++) {
  //  maskUV[i * 64 + i] = 1;
  //}
  // multOffset = 0;
  // for (int mult = 1; mult <= 10; mult++) {
  //  multOffset += CI * CI * (mult - 1);
  //  std::cout << "mult: " << mult << " " << multOffset << std::endl;
  //  for (int i = 0; i < CI * mult; i++) {
  //    for (int j = 0; j < CI; j++) {
  //      std::cout << maskUV[multOffset + i * CI + j] << " ";
  //    }
  //    std::cout << std::endl;
  //  }
  //  std::cout << std::endl;
  //  std::cout << std::endl;
  //}

  // prepare const(weights and bias)
  if (layerIn > 1) {
    int kernelLen = 2 * CI;
    for (int i = 0; i < 2 * CI * CO + CO; i++) {
      consts[i] = 0;
    }
    for (int lt = 0; lt < LT_NUM; lt++) {
      for (int idx = 0; idx < CO / LT_NUM; idx++) {
        int offsetY = (lt * CO / LT_NUM + idx) * kernelLen + (idx * LT_NUM + lt) / 4;

        int offsetU, offsetV;
        if (!reverseChannel) {
          offsetU = offsetY + CI - ((lt / 4) % 2);
          offsetV = offsetU + 1;
        } else {
          offsetV = offsetY + CI - ((lt / 4) % 2);
          offsetU = offsetV + 1;
        }

        // distribute contents of YUV terms
        int rIdx, gIdx, bIdx, zIdx;
        if (outputType == RGBA) {
          rIdx = 0;
          gIdx = 1;
          bIdx = 2;
          zIdx = 3;
        } else if (outputType == BGRA) {
          rIdx = 2;
          gIdx = 1;
          bIdx = 0;
          zIdx = 3;
        } else if (outputType == ARGB) {
          rIdx = 1;
          gIdx = 2;
          bIdx = 3;
          zIdx = 0;
        } else {
          rIdx = 3;
          gIdx = 2;
          bIdx = 1;
          zIdx = 0;
        }
        consts[idx * LT_NUM + lt + 2 * CI * CO] =  // bias
            (-222.912 * ((lt % 4) == rIdx) + 135.616 * ((lt % 4) == gIdx) + -276.800 * ((lt % 4) == bIdx));
        // Y
        ((int16_t*)consts)[offsetY] = ((lt % 4) != zIdx) * 0x253F;

        // U
        ((int16_t*)consts)[offsetU] = ((lt % 4) == gIdx) * (0xF375)   // G
                                      + ((lt % 4) == bIdx) * 0x408B;  // B
        // V
        ((int16_t*)consts)[offsetV] = ((lt % 4) == rIdx) * 0x3312       // R
                                      + ((lt % 4) == gIdx) * (0xE5FC);  // G

      }
    }
  }
  //  std::cout << "channelIn: " << channelIn << std::endl;
  //  std::cout << "channelOut: " << channelOut << std::endl;
  //  std::cout << "layerIn: " << layerIn << std::endl;
  //  std::cout << "layerOut: " << layerOut << std::endl;
  //  std::cout << std::endl;
  // half* maskUV_mlu;

  // malloc and copy consts_mlu
  int ecode = cnrtMalloc((void**)&((*param)->consts_mlu), (2 * CI * CO + CO) * sizeof(half));
  if (CNRT_RET_SUCCESS != ecode) {
    *estr = "Malloc consts FAILED! ERRCODE:" + to_string(ecode);
    FreeKernelParam(*param);
    free(consts);
    return -1;
  }
  ecode = cnrtMemcpy((*param)->consts_mlu, reinterpret_cast<half*>(consts), (2 * CI * CO + CO) * sizeof(half),
                     CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (CNRT_RET_SUCCESS != ecode) {
    *estr = "H2D consts FAILED! ERRCODE:" + to_string(ecode);
    FreeKernelParam(*param);
    free(consts);
    return -1;
  }
  free(consts);

  ecode = cnrtMalloc((void**)&((*param)->cycles), sizeof(uint32_t));
  if (CNRT_RET_SUCCESS != ecode) {
    *estr = "cnrt malloc failed. ERRCODE:" + to_string(ecode);
    FreeKernelParam(*param);
    return -1;
  }
  // // malloc and copy maskUV_mlu
  // if (CNRT_RET_SUCCESS !=
  //   cnrtMalloc((void**)&maskUV_mlu, CI * CI * total * sizeof(half))) {
  //   printf("cnrtMalloc FAILED!\n");
  //   exit(-1);
  // }
  // if (CNRT_RET_SUCCESS != cnrtMemcpy(maskUV_mlu, (half*)maskUV,
  //                                   CI * CI * total * sizeof(half),
  //                                    CNRT_MEM_TRANS_DIR_HOST2DEV)) {
  //   printf("cnrtMemcpy FAILED!\n");
  //   exit(-1);
  // }

  // func init param
  ecode = cnrtCreateKernelInitParam(&(*param)->func_init_param);
  if (CNRT_RET_SUCCESS != ecode) {
    (*param)->func_init_param = nullptr;
    *estr = "cnrt create kernel init param failed. ERRCODE:" + to_string(ecode);
    FreeKernelParam(*param);
    return -1;
  }

  ecode = cnrtInitKernelMemory(reinterpret_cast<void*>(&ResizeAndConvertKernelMlu270), (*param)->func_init_param);
  if (CNRT_RET_SUCCESS != ecode) {
    *estr = "cnrt init kernel memory failed. ERRCODE:" + to_string(ecode);
    FreeKernelParam(*param);
    return -1;
  }

  // params.
  (*param)->yuvFilter = (*param)->consts_mlu;
  (*param)->yuvBias = (*param)->consts_mlu + 2 * CI * CO;
  (*param)->s_row = s_row;
  (*param)->s_col = s_col;
  (*param)->d_row = d_row;
  (*param)->d_col = d_col;
  (*param)->roi_x = roi_x;
  (*param)->roi_y = roi_y;
  (*param)->roi_w = roi_w;
  (*param)->roi_h = roi_h;
  (*param)->channelIn = channelIn;
  (*param)->channelOut = channelOut;
  (*param)->layerIn = layerIn;
  (*param)->layerOut = layerOut;
  (*param)->input2half = input2half;
  (*param)->output2uint = output2uint;
  (*param)->scaleX = scaleX;
  (*param)->scaleY = scaleY;
  (*param)->batchNum = batchsize;
  return 0;
}

float reSizedConvert(half* dst, half* srcY, half* srcUV, KernelParam* kparam, cnrtFunctionType_t func_type,
                     cnrtDim3_t dim, cnrtQueue_t queue, int dev_type, string* estr) {
  int pad = 0;  // useless, make no difference
  cnrtKernelParamsBuffer_t params;
  cnrtGetKernelParamsBuffer(&params);
  cnrtKernelParamsBufferAddParam(params, &dst, sizeof(half*));
  cnrtKernelParamsBufferAddParam(params, &srcY, sizeof(half*));
  cnrtKernelParamsBufferAddParam(params, &srcUV, sizeof(half*));
  cnrtKernelParamsBufferAddParam(params, &kparam->yuvFilter, sizeof(half*));
  cnrtKernelParamsBufferAddParam(params, &kparam->yuvBias, sizeof(half*));
  // cnrtKernelParamsBufferAddParam(params, &kparam->maskUV_mlu, sizeof(half *));
  cnrtKernelParamsBufferAddParam(params, &kparam->s_row, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->s_col, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->d_row, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->d_col, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->roi_x, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->roi_y, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->roi_w, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->roi_h, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->input2half, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->output2uint, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->batchNum, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &pad, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->cycles, sizeof(uint32_t*));
  int ecode;
  if (1 == dev_type) {
    ecode = cnrtInvokeKernel_V2(reinterpret_cast<void*>(&ResizeAndConvertKernelMlu220), dim,
        params, func_type, queue);
  } else if (2 == dev_type) {
    ecode = cnrtInvokeKernel_V3(reinterpret_cast<void*>(&ResizeAndConvertKernelMlu270), kparam->func_init_param,
        dim, params, func_type, queue, nullptr);
  } else {
    ecode = cnrtInvokeKernel_V2(reinterpret_cast<void*>(&ResizeAndConvertKernel), dim, params,
      func_type, queue);
  }

  if (CNRT_RET_SUCCESS != ecode) {
    if (estr) {
      *estr = "[ResizeAndConvert] cnrtInvokeKernel FAILED. ERRCODE:" + to_string(ecode);
    }
    cnrtDestroyKernelParamsBuffer(params);
    return -1;
  }

  float _time = 0;

  uint32_t cycles = 0;
  ecode = cnrtMemcpy(&cycles, srcY, 1 * sizeof(uint32_t), CNRT_MEM_TRANS_DIR_DEV2HOST);
  if (CNRT_RET_SUCCESS != ecode) {
    if (estr) {
      *estr = "[ResizeAndConvert] memcpy cycles failed. ERRCODE:" + to_string(ecode);
    }
    cnrtDestroyKernelParamsBuffer(params);
    return -1;
  }
  _time = cycles * 0.04 / 1000;

  // free resources
  //  if (CNRT_RET_SUCCESS != cnrtFree(consts_mlu)) {
  //    printf("%s:%d cnrtFree FAILED!\n, __FILE__, __LINE__");
  //    exit(-1);
  //  }
  //
  //  if (CNRT_RET_SUCCESS != cnrtFree(maskUV_mlu)) {
  //    printf("%s:%d cnrtFree FAILED!\n, __FILE__, __LINE__");
  //    exit(-1);
  //  }

  ecode = cnrtDestroyKernelParamsBuffer(params);
  if (CNRT_RET_SUCCESS != ecode) {
    if (estr) {
      *estr = "[ResizeAndConvert] cnrtDestroyKernelParamsBuffer FAILED." + to_string(ecode);
    }
    return -1;
  }
  return _time;
}

float ResizeAndConvert(void* dst, void* srcY, void* srcUV, KernelParam* param, cnrtFunctionType_t func_type,
                       cnrtDim3_t dim, cnrtQueue_t queue, int dev_type, string* estr) {
  return reSizedConvert(reinterpret_cast<half*>(dst), reinterpret_cast<half*>(srcY), reinterpret_cast<half*>(srcUV),
                        param, func_type, dim, queue, dev_type, estr);
}
