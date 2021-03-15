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

#include "easybang/resize_yuv2yuv.h"

#include <deque>
#include <memory>
#include <string>
#include <utility>

#include "../../easyinfer/mlu_task_queue.h"

using std::string;

extern int PrepareKernelParam(int s_row, int s_col, int d_row, int d_col, int batch, uint32_t channel_id,
                              ResizeYUVKernelParam** param);

extern void FreeKernelParam(ResizeYUVKernelParam* param);

extern float resize_kernel(void** Ydst, void** UVdst, void** srcY, void** srcUV, ResizeYUVKernelParam* param,
                           cnrtFunctionType_t func_type, cnrtDim3_t dim, cnrtQueue_t queue, string* estr);

namespace edk {

class MluResizeYUVPrivate {
 public:
  MluResizeYUV::Attr attr_;
  cnrtDim3_t dim_ = {4, 1, 1};
  cnrtFunctionType_t ftype_ = CNRT_FUNC_TYPE_BLOCK;
  MluTaskQueue_t queue_ = nullptr;
  ResizeYUVKernelParam* kparam_ = nullptr;

  std::deque<std::pair<void*, void*>> src_yuv_ptrs_cache_;
  void **src_y_ptrs_cpu_ = nullptr, **src_uv_ptrs_cpu_ = nullptr;
  void **src_y_ptrs_mlu_ = nullptr, **src_uv_ptrs_mlu_ = nullptr;
  std::deque<std::pair<void*, void*>> dst_yuv_ptrs_cache_;
  void **dst_y_ptrs_cpu_ = nullptr, **dst_uv_ptrs_cpu_ = nullptr;
  void **dst_y_ptrs_mlu_ = nullptr, **dst_uv_ptrs_mlu_ = nullptr;

  std::string estr_;
};  // MluResziePrivate

MluResizeYUV::MluResizeYUV() { d_ptr_ = new MluResizeYUVPrivate; }

MluResizeYUV::~MluResizeYUV() {
  delete d_ptr_;
  d_ptr_ = nullptr;
}

const MluResizeYUV::Attr& MluResizeYUV::GetAttr() { return d_ptr_->attr_; }

MluTaskQueue_t MluResizeYUV::GetMluQueue() const { return d_ptr_->queue_; }

void MluResizeYUV::SetMluQueue(MluTaskQueue_t queue) { d_ptr_->queue_ = queue; }

std::string MluResizeYUV::GetLastError() const { return d_ptr_->estr_; }

bool MluResizeYUV::Init(const MluResizeYUV::Attr& attr) {
  d_ptr_->attr_ = attr;
  d_ptr_->src_y_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * attr.batch_size));
  d_ptr_->src_uv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * attr.batch_size));
  d_ptr_->dst_y_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * attr.batch_size));
  d_ptr_->dst_uv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * attr.batch_size));
  cnrtRet_t cnret;
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_y_ptrs_mlu_), sizeof(char*) * attr.batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Malloc mlu buffer failed. Error code:" + std::to_string(cnret);
    return false;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_uv_ptrs_mlu_), sizeof(char*) * attr.batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Malloc mlu buffer failed. Error code:" + std::to_string(cnret);
    return false;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_y_ptrs_mlu_), sizeof(char*) * attr.batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Malloc mlu buffer failed. Error code:" + std::to_string(cnret);
    return false;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->dst_uv_ptrs_mlu_), sizeof(char*) * attr.batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Malloc mlu buffer failed. Error code:" + std::to_string(cnret);
    return false;
  }

  switch (attr.core) {
    case 1:
      d_ptr_->ftype_ = CNRT_FUNC_TYPE_BLOCK;
      break;
    case 4:
      d_ptr_->ftype_ = CNRT_FUNC_TYPE_UNION1;
      break;
    case 16:
      d_ptr_->ftype_ = CNRT_FUNC_TYPE_UNION4;
      break;
    default:
      d_ptr_->estr_ = "Unsupport union mode. Only support 1(block), 4(u1), 16(u4)";
      return false;
  }

  d_ptr_->dim_.x = d_ptr_->attr_.core;
  d_ptr_->dim_.y = 1; d_ptr_->dim_.z = 1;

  d_ptr_->queue_ = std::make_shared<edk::MluTaskQueue>();
  if (CNRT_RET_SUCCESS != cnrtCreateQueue(&d_ptr_->queue_->queue)) {
    d_ptr_->estr_ = "cnrtCreateQueue failed";
    return false;
  }
  return 0 == ::PrepareKernelParam(d_ptr_->attr_.src_h, d_ptr_->attr_.src_w, d_ptr_->attr_.dst_h,
      d_ptr_->attr_.dst_w, d_ptr_->attr_.batch_size, d_ptr_->attr_.channel_id, &d_ptr_->kparam_);
}

int MluResizeYUV::InvokeOp(void* dstY, void* dstUV, void* srcY, void* srcUV) {
  if (nullptr == d_ptr_->queue_ || nullptr == d_ptr_->queue_->queue) {
    throw MluResizeYUVError("cnrt queue is null.");
  }
  if (d_ptr_->attr_.batch_size != 1) {
    throw MluResizeYUVError(
        "InvokeOp is vaild only if the batchsize is 1. Please Use BatchingUp "
        "and SyncOneOutput to replase InvokeOp.");
  }
  SrcBatchingUp(srcY, srcUV);
  DstBatchingUp(dstY, dstUV);
  void **dstY_out_ptr = nullptr;
  void **dstUV_out_ptr = nullptr;
  if (!SyncOneOutput(dstY_out_ptr, dstUV_out_ptr)) {
    return -1;
  }
  return 0;
}

void MluResizeYUV::SrcBatchingUp(void* src_y, void* src_uv) {
  d_ptr_->src_yuv_ptrs_cache_.push_back(std::make_pair(src_y, src_uv));
}

void MluResizeYUV::DstBatchingUp(void* dst_y, void* dst_uv) {
  d_ptr_->dst_yuv_ptrs_cache_.push_back(std::make_pair(dst_y, dst_uv));
}

bool MluResizeYUV::SyncOneOutput(void** Ydst, void** UVdst) {
  if (nullptr == d_ptr_->queue_ || nullptr == d_ptr_->queue_->queue) {
    throw MluResizeYUVError("cnrt queue is null.");
  }
  if (static_cast<int>(d_ptr_->src_yuv_ptrs_cache_.size()) < d_ptr_->attr_.batch_size ||
      static_cast<int>(d_ptr_->dst_yuv_ptrs_cache_.size()) < d_ptr_->attr_.batch_size) {
    d_ptr_->estr_ = "Batchsize is " + std::to_string(d_ptr_->attr_.batch_size) +
                    ", but input: " + std::to_string(d_ptr_->src_yuv_ptrs_cache_.size()) +
                    ", output: " + std::to_string(d_ptr_->dst_yuv_ptrs_cache_.size());
    return false;
  }

  Ydst = d_ptr_->dst_y_ptrs_mlu_;
  UVdst = d_ptr_->dst_uv_ptrs_mlu_;
  for (int bi = 0; bi < d_ptr_->attr_.batch_size; ++bi) {
    d_ptr_->src_y_ptrs_cpu_[bi] = d_ptr_->src_yuv_ptrs_cache_.front().first;
    d_ptr_->src_uv_ptrs_cpu_[bi] = d_ptr_->src_yuv_ptrs_cache_.front().second;
    d_ptr_->dst_y_ptrs_cpu_[bi] = d_ptr_->dst_yuv_ptrs_cache_.front().first;
    d_ptr_->dst_uv_ptrs_cpu_[bi] = d_ptr_->dst_yuv_ptrs_cache_.front().second;
    d_ptr_->src_yuv_ptrs_cache_.pop_front();
    d_ptr_->dst_yuv_ptrs_cache_.pop_front();
  }

  cnrtRet_t cnret;
  cnret = cnrtMemcpy(d_ptr_->src_y_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_y_ptrs_cpu_),
      sizeof(char*) * d_ptr_->attr_.batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Memcpy host to device failed. Error code: " + std::to_string(cnret);
    return false;
  }
  cnret = cnrtMemcpy(d_ptr_->src_uv_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->src_uv_ptrs_cpu_),
      sizeof(char*) * d_ptr_->attr_.batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Memcpy host to device failed. Error code: " + std::to_string(cnret);
    return false;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_y_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_y_ptrs_cpu_),
      sizeof(char*) * d_ptr_->attr_.batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Memcpy host to device failed. Error code: " + std::to_string(cnret);
    return false;
  }
  cnret = cnrtMemcpy(d_ptr_->dst_uv_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->dst_uv_ptrs_cpu_),
      sizeof(char*) * d_ptr_->attr_.batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Memcpy host to device failed. Error code: " + std::to_string(cnret);
    return false;
  }
  return -1 != ::resize_kernel(Ydst, UVdst, d_ptr_->src_y_ptrs_mlu_, d_ptr_->src_uv_ptrs_mlu_, d_ptr_->kparam_,
                               d_ptr_->ftype_, d_ptr_->dim_, d_ptr_->queue_->queue, &d_ptr_->estr_);
}

void MluResizeYUV::Destroy() {
  if (d_ptr_->kparam_) {
    ::FreeKernelParam(d_ptr_->kparam_);
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
}

}  // namespace edk
