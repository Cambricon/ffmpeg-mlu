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

#include <deque>
#include <memory>
#include <string>
#include <utility>

#include "cxxutil/logger.h"
#include "easybang/resize_and_colorcvt.h"
#include "easyinfer/mlu_context.h"
#include "../../easyinfer/mlu_task_queue.h"

using std::string;
extern int PrepareKernelParam(int s_row, int s_col, int d_row, int d_col, int roi_x, int roi_y, int roi_w, int roi_h,
                              int color_mode, int data_type, int bsize_, KernelParam** param, string* estr);

extern void FreeKernelParam(KernelParam* param);

extern float ResizeAndConvert(void* dst, void* srcY, void* srcUV, KernelParam* param, cnrtFunctionType_t ftype,
                              cnrtDim3_t dim, cnrtQueue_t queue, int dev_type, string* estr);

namespace edk {

class MluResizeConvertPrivate {
 public:
  MluResizeConvertOp::Attr attr_;
  cnrtFunctionType_t ftype_ = CNRT_FUNC_TYPE_BLOCK;
  MluTaskQueue_t queue_ = nullptr;
  KernelParam* kparam_ = nullptr;
  std::deque<std::pair<void*, void*>> yuv_ptrs_cache_;
  void *y_ptrs_cpu_ = nullptr, *uv_ptrs_cpu_ = nullptr;
  void *y_ptrs_mlu_ = nullptr, *uv_ptrs_mlu_ = nullptr;
  std::string estr_;
};

MluResizeConvertOp::MluResizeConvertOp() { d_ptr_ = new MluResizeConvertPrivate; }

MluResizeConvertOp::~MluResizeConvertOp() {
  delete d_ptr_;
  d_ptr_ = nullptr;
}

const MluResizeConvertOp::Attr& MluResizeConvertOp::GetAttr() { return d_ptr_->attr_; }

MluTaskQueue_t MluResizeConvertOp::GetMluQueue() const { return d_ptr_->queue_; }

void MluResizeConvertOp::SetMluQueue(MluTaskQueue_t queue) { d_ptr_->queue_ = queue; }

std::string MluResizeConvertOp::GetLastError() const { return d_ptr_->estr_; }

bool MluResizeConvertOp::Init(const MluResizeConvertOp::Attr& attr) {
  d_ptr_->attr_ = attr;
  uint32_t src_stride = attr.src_w > attr.src_stride ? attr.src_w : attr.src_stride;
  uint32_t crop_x = attr.crop_x >= attr.src_w ? 0 : attr.crop_x;
  uint32_t crop_y = attr.crop_y >= attr.src_h ? 0 : attr.crop_y;
  uint32_t crop_w = attr.crop_w == 0 ? attr.src_w : attr.crop_w;
  crop_w = (crop_w + crop_x) > attr.src_w ? (attr.src_w - crop_x) : crop_w;
  uint32_t crop_h = attr.crop_h == 0 ? attr.src_h : attr.crop_h;
  crop_h = (crop_h + crop_y) > attr.src_h ? (attr.src_h - crop_y) : crop_h;
  d_ptr_->attr_.src_stride = src_stride;
  d_ptr_->attr_.crop_x = crop_x;
  d_ptr_->attr_.crop_y = crop_y;
  d_ptr_->attr_.crop_w = crop_w;
  d_ptr_->attr_.crop_h = crop_h;

  d_ptr_->y_ptrs_cpu_ = malloc(sizeof(void*) * attr.batch_size);
  d_ptr_->uv_ptrs_cpu_ = malloc(sizeof(void*) * attr.batch_size);
  cnrtRet_t cnret = cnrtMalloc(&d_ptr_->y_ptrs_mlu_, sizeof(void*) * attr.batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Malloc mlu buffer failed. Error code:" + std::to_string(cnret);
    return false;
  }
  cnret = cnrtMalloc(&d_ptr_->uv_ptrs_mlu_, sizeof(void*) * attr.batch_size);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Malloc mlu buffer failed. Error code:" + std::to_string(cnret);
    return false;
  }

  switch (attr.batch_size) {
    case 1:
      d_ptr_->ftype_ = CNRT_FUNC_TYPE_BLOCK;
      break;
    case 4:
      d_ptr_->ftype_ = CNRT_FUNC_TYPE_UNION1;
      break;
    case 8:
      d_ptr_->ftype_ = CNRT_FUNC_TYPE_UNION2;
      break;
    case 16:
      d_ptr_->ftype_ = CNRT_FUNC_TYPE_UNION4;
      break;
    case 32:
      d_ptr_->ftype_ = CNRT_FUNC_TYPE_UNION8;
      break;
    default:
      d_ptr_->estr_ = "Unsupport batchsize. Only support 1, 4, 8, 16, 32";
      return false;
  }

  LOG(INFO, "Init ResizeAndConvert Operator");

  return 0 == ::PrepareKernelParam(d_ptr_->attr_.src_h, d_ptr_->attr_.src_stride, d_ptr_->attr_.dst_h,
                                   d_ptr_->attr_.dst_w, crop_x, crop_y, crop_w, crop_h,
                                   static_cast<int>(d_ptr_->attr_.color_mode),
                                   static_cast<int>(d_ptr_->attr_.data_mode), d_ptr_->attr_.batch_size,
                                   &d_ptr_->kparam_, &d_ptr_->estr_);
}

int MluResizeConvertOp::InvokeOp(void* dst, void* srcY, void* srcUV) {
  if (nullptr == d_ptr_->queue_ || nullptr == d_ptr_->queue_->queue) {
    throw MluResizeConvertOpError("cnrt queue is null.");
  }
  if (d_ptr_->attr_.batch_size != 1) {
    throw MluResizeConvertOpError(
        "InvokeOp is vaild only if the batchsize is 1. Please Use BatchingUp "
        "and SyncOneOutput to replase InvokeOp.");
  }
  BatchingUp(srcY, srcUV);
  if (!SyncOneOutput(dst)) {
    return -1;
  }
  return 0;
}

void MluResizeConvertOp::BatchingUp(void* src_y, void* src_uv) {
  LOG(TRACE, "Store resize and convert operator input for batching, %p, %p", src_y, src_uv);
  d_ptr_->yuv_ptrs_cache_.push_back(std::make_pair(src_y, src_uv));
}

bool MluResizeConvertOp::SyncOneOutput(void* dst) {
  if (nullptr == d_ptr_->queue_ || nullptr == d_ptr_->queue_->queue) {
    throw MluResizeConvertOpError("cnrt queue is null.");
  }
  if (static_cast<int>(d_ptr_->yuv_ptrs_cache_.size()) < d_ptr_->attr_.batch_size) {
    d_ptr_->estr_ = "Batchsize is " + std::to_string(d_ptr_->attr_.batch_size) + ", but only has" +
                    std::to_string(d_ptr_->yuv_ptrs_cache_.size());
    return false;
  }
  for (int bi = 0; bi < d_ptr_->attr_.batch_size; ++bi) {
    reinterpret_cast<void**>(d_ptr_->y_ptrs_cpu_)[bi] = d_ptr_->yuv_ptrs_cache_.front().first;
    reinterpret_cast<void**>(d_ptr_->uv_ptrs_cpu_)[bi] = d_ptr_->yuv_ptrs_cache_.front().second;
    d_ptr_->yuv_ptrs_cache_.pop_front();
  }
  cnrtRet_t cnret = cnrtMemcpy(d_ptr_->y_ptrs_mlu_, d_ptr_->y_ptrs_cpu_, sizeof(void*) * d_ptr_->attr_.batch_size,
                               CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Memcpy host to device failed. Error code: " + std::to_string(cnret);
    return false;
  }
  cnret = cnrtMemcpy(d_ptr_->uv_ptrs_mlu_, d_ptr_->uv_ptrs_cpu_, sizeof(void*) * d_ptr_->attr_.batch_size,
                     CNRT_MEM_TRANS_DIR_HOST2DEV);
  if (cnret != CNRT_RET_SUCCESS) {
    d_ptr_->estr_ = "Memcpy host to device failed. Error code: " + std::to_string(cnret);
    return false;
  }
  cnrtDim3_t dim;
  dim.x = d_ptr_->attr_.batch_size;
  dim.y = 1;
  dim.z = 1;

  LOG(TRACE, "Do resize and convert process, dst: %p", dst);
  return -1 != ::ResizeAndConvert(dst, d_ptr_->y_ptrs_mlu_, d_ptr_->uv_ptrs_mlu_, d_ptr_->kparam_, d_ptr_->ftype_, dim,
                                  d_ptr_->queue_->queue, static_cast<int>(d_ptr_->attr_.core_version), &d_ptr_->estr_);
}

void MluResizeConvertOp::Destroy() {
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
