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

#include <string>

#include "cnrt.h"
#include "ResizeKernel.h"

using std::string;
using std::to_string;

struct ResizeKernelParam {
  int s_row, s_col, d_row, d_col;
  int batch;
  cnrtKernelInitParam_t init_param = nullptr;
  uint32_t affinity;
};

int PrepareKernelParam(
    int s_row, int s_col, int d_row, int d_col, int batch, uint32_t channel_id, ResizeKernelParam** param) {
  *param = new ResizeKernelParam;
  (*param)->s_row = s_row;
  (*param)->s_col = s_col;
  (*param)->d_row = d_row;
  (*param)->d_col = d_col;
  (*param)->batch = batch;

  if (channel_id % 2 == 0) {
    cnrtSetCurrentChannel(CNRT_CHANNEL_TYPE_0);
    (*param)->affinity = 0x01;
  } else {
    cnrtSetCurrentChannel(CNRT_CHANNEL_TYPE_1);
    (*param)->affinity = 0x02;
  }

  cnrtCreateKernelInitParam(&(*param)->init_param);
  cnrtInitKernelMemory(reinterpret_cast<void*>(&resizeKernel), (*param)->init_param);

  return 0;
}

void FreeKernelParam(ResizeKernelParam* param) {
  if (param) {
    if (param->init_param) {
      cnrtDestroyKernelInitParamAndMemory(param->init_param);
    }
    delete param;
  }
}

float InvokeResizeKernel(char* dst, char** srcY, char** srcUV, ResizeKernelParam* kparam, cnrtFunctionType_t func_type,
              cnrtDim3_t dim, cnrtQueue_t queue, string* estr) {
  cnrtKernelParamsBuffer_t params;
  cnrtGetKernelParamsBuffer(&params);
  cnrtKernelParamsBufferAddParam(params, &dst, sizeof(char *));
  cnrtKernelParamsBufferAddParam(params, &srcY, sizeof(char **));
  cnrtKernelParamsBufferAddParam(params, &srcUV, sizeof(char **));
  cnrtKernelParamsBufferAddParam(params, &kparam->s_row, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->s_col, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->d_row, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->d_col, sizeof(int));
  cnrtKernelParamsBufferAddParam(params, &kparam->batch, sizeof(int));

  int ecode;

  if (func_type == CNRT_FUNC_TYPE_UNION1) {
    cnrtInvokeParam_t invoke_param;
    invoke_param.invoke_param_type = CNRT_INVOKE_PARAM_TYPE_0;
    invoke_param.cluster_affinity.affinity = &kparam->affinity;
    ecode = cnrtInvokeKernel_V3(reinterpret_cast<void*>(&resizeKernel), kparam->init_param, dim, params, func_type,
                                queue, reinterpret_cast<void*>(&invoke_param));
  } else {
    ecode = cnrtInvokeKernel_V3(reinterpret_cast<void*>(&resizeKernel), kparam->init_param, dim, params, func_type,
                                queue, NULL);
  }

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

float Resize(void* dst, void** srcY, void** srcUV, ResizeKernelParam* param, cnrtFunctionType_t func_type,
             cnrtDim3_t dim, cnrtQueue_t queue, string* estr) {
    return InvokeResizeKernel(reinterpret_cast<char*>(dst), reinterpret_cast<char**>(srcY),
                              reinterpret_cast<char**>(srcUV), param, func_type, dim, queue, estr);
}

