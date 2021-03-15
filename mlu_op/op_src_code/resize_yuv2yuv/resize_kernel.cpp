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
#include <iostream>
#include <string>
#include <chrono>

#include "cnrt.h"
#include "resizeYuvKernel.h"

#define PRINT_TIME 0

using std::string;
using std::to_string;

struct ResizeYUVKernelParam {
  int s_row, s_col, d_row, d_col;
  int batch;
  cnrtKernelInitParam_t init_param = nullptr;
  uint32_t affinity;
};

int PrepareKernelParam(
    int s_row, int s_col, int d_row, int d_col, int batch, uint32_t channel_id, ResizeYUVKernelParam** param) {
  *param = new ResizeYUVKernelParam;
  (*param)->s_row = s_row;
  (*param)->s_col = s_col;
  (*param)->d_row = d_row;
  (*param)->d_col = d_col;
  (*param)->batch = batch;

  // if (channel_id % 4 == 0) {
  //   cnrtSetCurrentChannel(CNRT_CHANNEL_TYPE_0);
  //   (*param)->affinity = 0x01;
  // } else {
  //   cnrtSetCurrentChannel(CNRT_CHANNEL_TYPE_1);
  //   (*param)->affinity = 0x02;
  // }

  cnrtCreateKernelInitParam(&(*param)->init_param);
  cnrtInitKernelMemory(reinterpret_cast<void*>(&resizeYuvKernel), (*param)->init_param);

  return 0;
}

void FreeKernelParam(ResizeYUVKernelParam* param) {
  if (param) {
    if (param->init_param) {
      cnrtDestroyKernelInitParamAndMemory(param->init_param);
    }
    delete param;
  }
}

float InvokeResizeYUVKernel(char** Ydst, char** UVdst, char** srcY, char** srcUV, ResizeYUVKernelParam* kparam, cnrtFunctionType_t func_type,
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
    if (estr) {
      *estr = "[Resize] cnrtInvokeKernel FAILED. ERRCODE:" + to_string(ecode);
    }
    cnrtDestroyKernelParamsBuffer(params);
    return -1;
  }
  ecode = cnrtDestroyKernelParamsBuffer(params);
  if (CNRT_RET_SUCCESS != ecode) {
    if (estr) {
      *estr = "[Resize] cnrtDestroyKernelParamsBuffer FAILED." + to_string(ecode);
    }
    return -1;
  }

  return 0;
}

float resize_kernel(void** Ydst, void** UVdst, void** srcY, void** srcUV, ResizeYUVKernelParam* param,
                    cnrtFunctionType_t func_type, cnrtDim3_t dim, cnrtQueue_t queue, string* estr) {
    return InvokeResizeYUVKernel(reinterpret_cast<char**>(Ydst), reinterpret_cast<char**>(UVdst), reinterpret_cast<char**>(srcY),
                              reinterpret_cast<char**>(srcUV), param, func_type, dim, queue, estr);
}

