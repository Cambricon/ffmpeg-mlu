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

// #include "mluop.h"
#include "mluop_context.hpp"

int process_resize_rgbx(param_ctx_t ctx);
void rgbx2rgbx_resize_op(params_conf &op_conf) {
  param_ctx_t ctx;
  ctx.input_file  = op_conf.find("input_file")->second;
  ctx.output_file = op_conf.find("output_file")->second;
  ctx.src_pix_fmt = op_conf.find("src_pix_fmt")->second;

  ctx.src_w      = std::atoi(op_conf.find("src_w")->second.c_str());
  ctx.src_h      = std::atoi(op_conf.find("src_h")->second.c_str());
  ctx.dst_w      = std::atoi(op_conf.find("dst_w")->second.c_str());
  ctx.dst_h      = std::atoi(op_conf.find("dst_h")->second.c_str());
  ctx.frame_num  = std::atoi(op_conf.find("frame_num")->second.c_str());
  ctx.thread_num = std::atoi(op_conf.find("thread_num")->second.c_str());
  ctx.save_flag  = std::atoi(op_conf.find("save_flag")->second.c_str());
  ctx.device_id  = std::atoi(op_conf.find("device_id")->second.c_str());

  std::vector<std::thread> thd_vec;
  for (uint32_t i = 0; i < ctx.thread_num; i++) {
    std::cout << "create thead [" << i << "]" << std::endl;
    thd_vec.emplace_back(std::thread(process_resize_rgbx, ctx));
  }
  std::vector<std::thread>::iterator iter;
  for (iter = thd_vec.begin(); iter != thd_vec.end(); iter++) {
    iter->join();
  }
  thd_vec.clear();
}

int process_resize_rgbx(param_ctx_t ctx) {
  bool save_flag = ctx.save_flag;
  uint32_t input_w = ctx.src_w;
  uint32_t input_h =  ctx.src_h;
  uint32_t dst_w = ctx.dst_w;
  uint32_t dst_h = ctx.dst_h;
  uint32_t frame_num = ctx.frame_num;
  uint32_t device_id = ctx.device_id;
  uint32_t pix_chn_num = getPixFmtChannelNum(
                          getCNCVPixFmtFromPixindex(ctx.src_pix_fmt));
  const char *depth = "8U";
  std::string filename = ctx.input_file;
  std::string output_file =ctx.output_file;

  set_cnrt_ctx(device_id, CNRT_CHANNEL_TYPE_NONE);

  uint32_t src_stride = PAD_UP(input_w, ALIGN_R_SCALE) * pix_chn_num;
  uint32_t dst_stride = PAD_UP(dst_w, ALIGN_R_SCALE) * pix_chn_num;
  uint32_t src_size = input_h * src_stride;
  uint32_t dst_size = dst_h * dst_stride;
  void *src_cpu = (void *)malloc(src_size);
  void *dst_cpu = (void *)malloc(dst_size);

  cv::Mat src_mat;
  cv::Mat dst_mat;
  dst_mat = cv::Mat(dst_h, dst_w, CV_8UC3, cv::Scalar(0, 0, 0));
  src_mat = cv::imread(filename, cv::IMREAD_COLOR);
  if (!strcmp(ctx.src_pix_fmt.c_str(), "rgb24"))
    cv::cvtColor(src_mat, src_mat, cv::COLOR_BGR2RGB);
  else if (!strcmp(ctx.src_pix_fmt.c_str(), "rgba"))
    cv::cvtColor(src_mat, src_mat, cv::COLOR_BGR2RGBA);
  else if (!strcmp(ctx.src_pix_fmt.c_str(), "bgra"))
    cv::cvtColor(src_mat, src_mat, cv::COLOR_BGR2BGRA);
  // else if (!strcmp(ctx.src_pix_fmt.c_str(), "abgr"))
  //   cv::cvtColor(src_mat, src_mat, cv::COLOR_BGR2ABGR);
  // else if (!strcmp(ctx.src_pix_fmt.c_str(), "argb"))
  //   cv::cvtColor(src_mat, src_mat, cv::COLOR_BGR2ARGB);
  memcpy(src_cpu, src_mat.data, src_size);

  HANDLE handle;
  mluOpAPI mluop_api;
  std::shared_ptr<mluOpFuncList> op_funcs;
  op_funcs = mluop_api.getAPI();
  std::cout << "MLUOP_VERSION:" << op_funcs->mluOpGetVersion() << std::endl;

  double op_time = 0.0;
  timeWatch op_watcher;
  op_watcher.start();

  int ret;
  ret = op_funcs->mluScaleRgbxInit(&handle, input_w, input_h,
                              dst_w, dst_h, ctx.src_pix_fmt.c_str(), depth);
  if (ret) {
    std::cout << "resize rgbx op init failed" << std::endl;
    exit(1);
  }
  op_time = op_watcher.stop();
  std::cout << "init time:" << op_time << "ms" << std::endl;

  void *src_mlu;
  void *dst_mlu;
  CNRT_CHECK(cnrtMalloc((void **)(&src_mlu), src_size));
  CNRT_CHECK(cnrtMalloc((void **)(&dst_mlu), dst_size));
  CNRT_CHECK(cnrtMemcpy(src_mlu, src_cpu, src_size, CNRT_MEM_TRANS_DIR_HOST2DEV));
  CNRT_CHECK(cnrtMemcpy(src_mlu, src_cpu, src_size, CNRT_MEM_TRANS_DIR_HOST2DEV));

  op_time = 0.0;
  for (uint32_t i = 0; i < frame_num; i++) {
    op_watcher.start();
    ret = op_funcs->mluScaleRgbxExec(handle, src_mlu, dst_mlu);
    // ret = op_funcs->mluScaleRgbxExecPad(handle, src_mlu, dst_mlu);
    // ret = op_funcs->mluScaleRgbxExecCrop(handle, src_mlu, dst_mlu, 0,0,960,960,0,0,480,480);
    if (ret) {
      std::cout << "resize rgbx op init failed" << std::endl;
      exit(1);
    }
    op_time += op_watcher.stop();
  }
  std::cout << "exec time(ave.):" << op_time / frame_num
            << "ms, total frames:" << frame_num << std::endl;

  cnrtMemcpy(dst_cpu, dst_mlu, dst_size, CNRT_MEM_TRANS_DIR_DEV2HOST);

  op_time = 0.0;
  op_watcher.start();
  ret = op_funcs->mluScaleRgbxDestroy(handle);
  if (ret) {
    std::cout << "resize rgbx op init failed" << std::endl;
    exit(1);
  }
  op_time = op_watcher.stop();
  std::cout << "destroy time:" << op_time << "ms" << std::endl;

  if (save_flag) {
    dst_mat.create(dst_h, dst_w, (pix_chn_num == 3 ? CV_8UC3:CV_8UC4));
    for (uint32_t row = 0; row < dst_h; ++row) {
        memcpy(dst_mat.ptr<uint8_t>(row),
                reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(dst_cpu) +
                                 row * dst_stride),
                dst_mat.cols * dst_mat.elemSize());
    }
    cv::imwrite(output_file, dst_mat);
  }

  if (src_cpu) free(src_cpu);
  if (src_mlu) cnrtFree(src_mlu);
  if (dst_cpu) free(dst_cpu);
  if (dst_mlu) cnrtFree(dst_mlu);

  return 0;
}
