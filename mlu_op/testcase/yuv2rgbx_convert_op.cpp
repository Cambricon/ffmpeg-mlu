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

int process_convert_yuv2rgbx(param_ctx_t ctx);

void yuv2rgbx_convert_op(params_conf &op_conf) {
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
    thd_vec.emplace_back(std::thread(process_convert_yuv2rgbx, ctx));
  }
  std::vector<std::thread>::iterator iter;
  for (iter = thd_vec.begin(); iter != thd_vec.end(); iter++) {
    iter->join();
  }
  thd_vec.clear();
}

int process_convert_yuv2rgbx(param_ctx_t ctx) {
  bool save_flag = ctx.save_flag;
  uint32_t width = ctx.src_w;
  uint32_t height =  ctx.src_h;
  uint32_t frame_num = ctx.frame_num;
  uint32_t device_id = ctx.device_id;
  uint32_t dst_pix_chn_num = getPixFmtChannelNum(
                              getCNCVPixFmtFromPixindex(ctx.dst_pix_fmt));
  const char *depth = "8U";
  std::string filename = ctx.input_file;
  std::string output_file =ctx.output_file;

  set_cnrt_ctx(device_id);

  uint32_t src_y_stride = PAD_UP(width, ALIGN_Y2R_CVT);
  uint32_t src_uv_stride = PAD_UP(width, ALIGN_Y2R_CVT);
  uint32_t dst_stride = PAD_UP(width, ALIGN_Y2R_CVT) * dst_pix_chn_num;
  uint32_t src_y_size = height * src_y_stride;
  uint32_t src_uv_size = height * src_uv_stride * 3 / 2;
  uint32_t src_size = src_y_size + src_uv_size;
  uint32_t dst_size = height * dst_stride;

  uint8_t *src_cpu = (uint8_t *)malloc(src_size);
  uint8_t *dst_cpu = (uint8_t *)malloc(dst_size);

  cv::Mat src_mat;
  cv::Mat src_yuv_mat;
  cv::Mat dst_mat;
  src_mat = cv::imread(filename, cv::IMREAD_COLOR);
  !strcmp(ctx.src_pix_fmt.c_str(), "nv12")?
    BGR24_TO_NV12(src_mat, src_yuv_mat):
    BGR24_TO_NV21(src_mat, src_yuv_mat);

  for (uint32_t row = 0; row < height; ++row) {
    memcpy(reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(src_cpu) +
                                       row * src_y_stride),
           src_yuv_mat.ptr<uint8_t>(row),
           src_yuv_mat.cols * src_yuv_mat.elemSize());
  }
  for (uint32_t row = 0; row < height / 2; ++row) {
    memcpy(reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(src_cpu) + src_y_size +
                                       row * src_uv_stride),
           src_yuv_mat.ptr<uint8_t>(row + height),
           src_yuv_mat.cols * src_yuv_mat.elemSize());
  }

  void *src_y_mlu;
  void *src_uv_mlu;
  void *dst_mlu;
  cnrtMalloc((void **)(&src_y_mlu), src_y_size);
  cnrtMalloc((void **)(&src_uv_mlu), src_uv_size);
  cnrtMalloc((void **)(&dst_mlu), dst_size);
  cnrtMemcpy(src_y_mlu, src_cpu, src_y_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  cnrtMemcpy(src_uv_mlu, (src_cpu + src_y_size), src_uv_size, CNRT_MEM_TRANS_DIR_HOST2DEV);

  HANDLE handle;
  mluOpAPI mluop_api;
  std::shared_ptr<mluOpFuncList> op_funcs;
  op_funcs = mluop_api.getAPI();
  std::cout << "MLUOP_VERSION:" << op_funcs->mluOpGetVersion() << std::endl;

  double op_time = 0.0;
  timeWatch op_watcher;
  op_watcher.start();

  int ret;
  ret = op_funcs->mluCvtYuv2RgbxInit(&handle, width, height,
                                    ctx.src_pix_fmt.c_str(),
                                    ctx.dst_pix_fmt.c_str(),
                                    depth);
  if (ret) {
    std::cout << "convert yuv2rgbx op init failed" << std::endl;
    exit(1);
  }
  op_time = op_watcher.stop();
  std::cout << "init time:" << op_time << "ms" << std::endl;

  cnrtMemcpy(src_y_mlu, src_cpu, src_y_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  cnrtMemcpy(src_uv_mlu, (src_cpu + src_y_size), src_uv_size, CNRT_MEM_TRANS_DIR_HOST2DEV);

  op_time = 0.0;
  for (uint32_t i = 0; i < frame_num; i++) {
    op_watcher.start();
    ret = op_funcs->mluCvtYuv2RgbxExec(handle, src_y_mlu, src_uv_mlu, dst_mlu);
    if (ret) {
      std::cout << "convert yuv2rgbx exec failed" << std::endl;
      exit(-1);
    }
    op_time += op_watcher.stop();
  }
  std::cout << "exec time(ave.):" << op_time / frame_num
            << "ms, total frames:" << frame_num << std::endl;

  cnrtMemcpy(dst_cpu, dst_mlu, dst_size, CNRT_MEM_TRANS_DIR_DEV2HOST);

  op_time = 0.0;
  op_watcher.start();
  ret = op_funcs->mluCvtYuv2RgbxDestroy(handle);
  if (ret) {
    std::cout << "convert yuv2rgbx destroy failed" << std::endl;
    exit(-1);
  }
  op_time = op_watcher.stop();
  std::cout << "destroy time:" << op_time << "ms" << std::endl;

  if (save_flag){
    dst_mat.create(height, width, CV_8UC(dst_pix_chn_num));
    for (uint32_t row = 0; row < height; ++row) {
        memcpy(dst_mat.ptr<uint8_t>(row),
              reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(dst_cpu) +
                                row * dst_stride),
              dst_mat.cols * dst_mat.elemSize());
    }
    cv::imwrite(output_file, dst_mat);
  }

  if (src_cpu)    free(src_cpu);
  if (src_y_mlu)  cnrtFree(src_y_mlu);
  if (src_uv_mlu) cnrtFree(src_uv_mlu);
  if (dst_cpu)    free(dst_cpu);
  if (dst_mlu)    cnrtFree(dst_mlu);

  return 0;
}
