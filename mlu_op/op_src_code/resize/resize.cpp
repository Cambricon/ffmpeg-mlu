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

#include "easybang/resize.h"

#include <deque>
#include <memory>
#include <string>
#include <utility>

#include "../../easyinfer/mlu_task_queue.h"

using std::string;

extern int PrepareKernelParam(int s_row, int s_col, int d_row, int d_col, int batch, uint32_t channel_id,
                              ResizeKernelParam** param);

extern void FreeKernelParam(ResizeKernelParam* param);

extern float Resize(void* dst, void** srcY, void** srcUV, ResizeKernelParam* param, cnrtFunctionType_t func_type,
                    cnrtDim3_t dim, cnrtQueue_t queue, string* estr);

namespace edk {

class MluResizePrivate {
 public:
  MluResize::Attr attr_;
  cnrtFunctionType_t ftype_ = CNRT_FUNC_TYPE_BLOCK;
  MluTaskQueue_t queue_ = nullptr;
  ResizeKernelParam* kparam_ = nullptr;
  std::deque<std::pair<void*, void*>> yuv_ptrs_cache_;
  void **y_ptrs_cpu_ = nullptr, **uv_ptrs_cpu_ = nullptr;
  void **y_ptrs_mlu_ = nullptr, **uv_ptrs_mlu_ = nullptr;
  std::string estr_;
};  // MluResziePrivate

MluResize::MluResize() { d_ptr_ = new MluResizePrivate; }

MluResize::~MluResize() {
  delete d_ptr_;
  d_ptr_ = nullptr;
}

const MluResize::Attr& MluResize::GetAttr() { return d_ptr_->attr_; }

MluTaskQueue_t MluResize::GetMluQueue() const { return d_ptr_->queue_; }

void MluResize::SetMluQueue(MluTaskQueue_t queue) { d_ptr_->queue_ = queue; }

std::string MluResize::GetLastError() const { return d_ptr_->estr_; }

bool MluResize::Init(const MluResize::Attr& attr) {
  d_ptr_->attr_ = attr;

  d_ptr_->y_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * attr.batch_size));
  d_ptr_->uv_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(char*) * attr.batch_size));
  cnrtRet_t cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->y_ptrs_mlu_), sizeof(char*) * attr.batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Malloc mlu buffer failed. Error code:" + std::to_string(cnret);
    return false;
  }
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->uv_ptrs_mlu_), sizeof(char*) * attr.batch_size);
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
    case 8:
      d_ptr_->ftype_ = CNRT_FUNC_TYPE_UNION2;
      break;
    default:
      d_ptr_->estr_ = "Unsupport union mode. Only support 1(block), 4(u1), 8(u2)";
      return false;
  }

  d_ptr_->queue_ = std::make_shared<edk::MluTaskQueue>();
  if (CNRT_RET_SUCCESS != cnrtCreateQueue(&d_ptr_->queue_->queue)) {
    d_ptr_->estr_ = "cnrtCreateQueue failed";
    return false;
  }
  return 0 == ::PrepareKernelParam(d_ptr_->attr_.src_h, d_ptr_->attr_.src_w, d_ptr_->attr_.dst_h,
      d_ptr_->attr_.dst_w, d_ptr_->attr_.batch_size, d_ptr_->attr_.channel_id, &d_ptr_->kparam_);
}

int MluResize::InvokeOp(void* dst, void* srcY, void* srcUV) {
  if (nullptr == d_ptr_->queue_ || nullptr == d_ptr_->queue_->queue) {
    throw MluResizeError("cnrt queue is null.");
  }
  if (d_ptr_->attr_.batch_size != 1) {
    throw MluResizeError(
        "InvokeOp is vaild only if the batchsize is 1. Please Use BatchingUp "
        "and SyncOneOutput to replase InvokeOp.");
  }
  BatchingUp(srcY, srcUV);
  if (!SyncOneOutput(dst)) {
    return -1;
  }
  return 0;
}

void MluResize::BatchingUp(void* src_y, void* src_uv) {
  d_ptr_->yuv_ptrs_cache_.push_back(std::make_pair(src_y, src_uv));
}

bool MluResize::SyncOneOutput(void* dst) {
  if (nullptr == d_ptr_->queue_ || nullptr == d_ptr_->queue_->queue) {
    throw MluResizeError("cnrt queue is null.");
  }
  if (static_cast<int>(d_ptr_->yuv_ptrs_cache_.size()) < d_ptr_->attr_.batch_size) {
    d_ptr_->estr_ = "Batchsize is " + std::to_string(d_ptr_->attr_.batch_size) + ", but only has" +
      std::to_string(d_ptr_->yuv_ptrs_cache_.size());
    return false;
  }
  for (int bi = 0; bi < d_ptr_->attr_.batch_size; ++bi) {
    d_ptr_->y_ptrs_cpu_[bi] = d_ptr_->yuv_ptrs_cache_.front().first;
    d_ptr_->uv_ptrs_cpu_[bi] = d_ptr_->yuv_ptrs_cache_.front().second;
    d_ptr_->yuv_ptrs_cache_.pop_front();
  }
  cnrtRet_t cnret = cnrtMemcpy(d_ptr_->y_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->y_ptrs_cpu_),
      sizeof(char*) * d_ptr_->attr_.batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Memcpy host to device failed. Error code: " + std::to_string(cnret);
    return false;
  }
  cnret = cnrtMemcpy(d_ptr_->uv_ptrs_mlu_, reinterpret_cast<void**>(d_ptr_->uv_ptrs_cpu_),
      sizeof(char*) * d_ptr_->attr_.batch_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Memcpy host to device failed. Error code: " + std::to_string(cnret);
    return false;
  }
  cnrtDim3_t dim;
  dim.x = d_ptr_->attr_.core;
  dim.y = 1;
  dim.z = 1;
  return -1 != ::Resize(dst, d_ptr_->y_ptrs_mlu_, d_ptr_->uv_ptrs_mlu_, d_ptr_->kparam_, d_ptr_->ftype_, dim,
      d_ptr_->queue_->queue, &d_ptr_->estr_);
}

void MluResize::Destroy() {
  if (d_ptr_->kparam_) {
    ::FreeKernelParam(d_ptr_->kparam_);
    d_ptr_->kparam_ = nullptr;
  }
  if (d_ptr_->y_ptrs_cpu_) {
    free(d_ptr_->y_ptrs_cpu_);
    d_ptr_->y_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->uv_ptrs_cpu_) {
    free(d_ptr_->uv_ptrs_cpu_);
    d_ptr_->uv_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->y_ptrs_mlu_) {
    cnrtFree(d_ptr_->y_ptrs_mlu_);
    d_ptr_->y_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->uv_ptrs_mlu_) {
    cnrtFree(d_ptr_->uv_ptrs_mlu_);
    d_ptr_->uv_ptrs_mlu_ = nullptr;
  }
  d_ptr_->yuv_ptrs_cache_.clear();
}

}  // namespace edk
