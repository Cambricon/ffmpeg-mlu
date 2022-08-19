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

int process_convert_rgbx2rgbx(param_ctx_t ctx);
void rgbx2rgbx_convert_op(params_conf &op_conf) {
  param_ctx_t ctx;
  ctx.input_file  = op_conf.find("input_file")->second;
  ctx.output_file = op_conf.find("output_file")->second;
  ctx.src_pix_fmt = op_conf.find("src_pix_fmt")->second;
  ctx.dst_pix_fmt = op_conf.find("dst_pix_fmt")->second;

  ctx.src_w      = std::atoi(op_conf.find("src_w")->second.c_str());
  ctx.src_h      = std::atoi(op_conf.find("src_h")->second.c_str());
  ctx.frame_num  = std::atoi(op_conf.find("frame_num")->second.c_str());
  ctx.thread_num = std::atoi(op_conf.find("thread_num")->second.c_str());
  ctx.save_flag  = std::atoi(op_conf.find("save_flag")->second.c_str());
  ctx.device_id  = std::atoi(op_conf.find("device_id")->second.c_str());

  std::vector<std::thread> thd_vec;
  for (uint32_t i = 0; i < ctx.thread_num; i++) {
    std::cout << "create thead [" << i << "]" << std::endl;
    thd_vec.emplace_back(std::thread(process_convert_rgbx2rgbx, ctx));
  }
  std::vector<std::thread>::iterator iter;
  for (iter = thd_vec.begin(); iter != thd_vec.end(); iter++) {
    iter->join();
  }
  thd_vec.clear();
}

int process_convert_rgbx2rgbx(param_ctx_t ctx) {
  bool save_flag = ctx.save_flag;
  uint32_t width = ctx.src_w;
  uint32_t height = ctx.src_h;
  uint32_t device_id = ctx.device_id;
  uint32_t src_pix_chn_num = getPixFmtChannelNum(getCNCVPixFmtFromPixindex(ctx.src_pix_fmt));
  uint32_t dst_pix_chn_num = getPixFmtChannelNum(getCNCVPixFmtFromPixindex(ctx.dst_pix_fmt));
  uint32_t frame_num = ctx.frame_num;
  const char *depth = "8U";
  std::string filename = ctx.input_file;
  std::string output_file = ctx.output_file;

  set_cnrt_ctx(device_id, CNRT_CHANNEL_TYPE_NONE);

  uint32_t src_stride = PAD_UP(width, ALIGN_R2Y_CVT) * src_pix_chn_num;
  uint32_t dst_stride = PAD_UP(width, ALIGN_R2Y_CVT) * dst_pix_chn_num;
  uint32_t src_size = height * src_stride;
  uint32_t dst_size = height * dst_stride;

  uint8_t *src_cpu = (uint8_t *)malloc(src_size);
  uint8_t *dst_cpu = (uint8_t *)malloc(dst_size);

  cv::Mat src_img = cv::imread(filename, cv::IMREAD_COLOR);
  if (!strcmp(ctx.src_pix_fmt.c_str(), "rgb24"))
    cv::cvtColor(src_img, src_img, cv::COLOR_BGR2RGB);
  else if (!strcmp(ctx.src_pix_fmt.c_str(), "rgba"))
    cv::cvtColor(src_img, src_img, cv::COLOR_BGR2RGBA);
  else if (!strcmp(ctx.src_pix_fmt.c_str(), "bgra"))
    cv::cvtColor(src_img, src_img, cv::COLOR_BGR2BGRA);
  // else if (!strcmp(ctx.src_pix_fmt.c_str(), "abgr"))
  //   cv::cvtColor(src_img, src_img, cv::COLOR_BGR2ABGR);
  // else if (!strcmp(ctx.src_pix_fmt.c_str(), "argb"))
  //   cv::cvtColor(src_img, src_img, cv::COLOR_BGR2ARGB);
  memcpy(src_cpu, src_img.data, src_size);

  void *src_rgbx_mlu;
  void *dst_rgbx_mlu;
  cnrtMalloc((void **)(&src_rgbx_mlu), src_size);
  cnrtMalloc((void **)(&dst_rgbx_mlu), dst_size);
  cnrtMemcpy(src_rgbx_mlu, src_cpu, src_size, CNRT_MEM_TRANS_DIR_HOST2DEV);

  HANDLE handle;
  mluOpAPI mluop_api;
  std::shared_ptr<mluOpFuncList> op_funcs;
  op_funcs = mluop_api.getAPI();
  std::cout << "MLUOP_VERSION:" << op_funcs->mluOpGetVersion() << std::endl;

  double op_time = 0.0;
  timeWatch op_watcher;
  op_watcher.start();

  printf("src pixfmt:%s, dst pixfmt:%s\n", ctx.src_pix_fmt.c_str(), ctx.dst_pix_fmt.c_str());

  int ret = 0;
  ret = op_funcs->mluCvtRgbx2RgbxInit(&handle, width, height,
                                  ctx.src_pix_fmt.c_str(),
                                  ctx.dst_pix_fmt.c_str(), depth);
  if (ret) {
    std::cout << "convert rgbx2rgbx op init failed" << std::endl;
    exit(1);
  }
  op_time = op_watcher.stop();
  std::cout << "init time:" << op_time << "ms" << std::endl;

  cnrtMemcpy(src_rgbx_mlu, src_cpu, src_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  for (uint32_t i = 0; i < frame_num; i++) {
    op_watcher.start();
    ret = op_funcs->mluCvtRgbx2RgbxExec(handle, src_rgbx_mlu, dst_rgbx_mlu);
    if (ret) {
      std::cout << "convert rgbx2rgbx op exec failed" << std::endl;
      exit(1);
    }
    op_time += op_watcher.stop();
  }
  std::cout << "exec time(ave.):" << op_time / frame_num
            << "ms, total frames:" << frame_num << std::endl;

  cnrtMemcpy(dst_cpu, dst_rgbx_mlu, dst_size, CNRT_MEM_TRANS_DIR_DEV2HOST);

  op_time = 0.0;
  op_watcher.start();
  ret = op_funcs->mluCvtRgbx2RgbxDestroy(handle);
  if (ret) {
    std::cout << "convert rgbx2rgbx op destroy failed" << std::endl;
    exit(1);
  }
  op_time = op_watcher.stop();
  std::cout << "destroy time:" << op_time << "ms" << std::endl;

  if (save_flag) {
    cv::Mat dst_mat;
    dst_mat.create(height, width, (dst_pix_chn_num == 3 ? CV_8UC3:CV_8UC4));
    for (uint32_t row = 0; row < height; ++row) {
      memcpy(dst_mat.ptr<uint8_t>(row),
             reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(dst_cpu) +
                                         row * dst_stride),
             dst_mat.cols * dst_mat.elemSize());
    }
    cv::imwrite(output_file, dst_mat);
  }

  if (src_cpu)      free(src_cpu);
  if (src_rgbx_mlu) cnrtFree(src_rgbx_mlu);
  if (dst_cpu)      free(dst_cpu);
  if (dst_rgbx_mlu) cnrtFree(dst_rgbx_mlu);

  return 0;
}
