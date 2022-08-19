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

int process_resize_yuv(param_ctx_t ctx);

void yuv2yuv_resize_op(params_conf &op_conf) {
  param_ctx_t ctx;
  ctx.exec_mod    = op_conf.find("exec_mod")->second;
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
    thd_vec.emplace_back(std::thread(process_resize_yuv, ctx));
  }
  std::vector<std::thread>::iterator iter;
  for (iter = thd_vec.begin(); iter != thd_vec.end(); iter++) {
    iter->join();
  }
  thd_vec.clear();
}

int process_resize_yuv(param_ctx_t ctx) {
  bool save_flag = ctx.save_flag;
  uint32_t width = ctx.src_w;
  uint32_t height =  ctx.src_h;
  uint32_t dst_w = ctx.dst_w;
  uint32_t dst_h = ctx.dst_h;
  uint32_t frame_num = ctx.frame_num;
  uint32_t device_id = ctx.device_id;
  const char *depth = "8U";
  std::string filename = ctx.input_file;
  std::string output_file =ctx.output_file;
  set_cnrt_ctx(device_id, CNRT_CHANNEL_TYPE_NONE);

  int yuv_size = width * height * 3 / 2;
  uint8_t *input_cpu_yuv = (uint8_t *)malloc(yuv_size * sizeof(uint8_t));

  cv::Mat yuv420sp_img;
  cv::Mat src_img = cv::imread(filename, cv::IMREAD_COLOR);
  !strcmp(ctx.src_pix_fmt.c_str(), "nv12")?
    BGR24_TO_NV12(src_img, yuv420sp_img):
    BGR24_TO_NV21(src_img, yuv420sp_img);
  memcpy(input_cpu_yuv, yuv420sp_img.data, yuv_size * sizeof(uint8_t));

  HANDLE handle;
  mluOpAPI mluop_api;
  std::shared_ptr<mluOpFuncList> op_funcs;
  op_funcs = mluop_api.getAPI();
  std::cout << "MLUOP_VERSION:" << op_funcs->mluOpGetVersion() << std::endl;

  double op_time = 0.0;
  timeWatch op_watcher;
  op_watcher.start();

  int ret = 0;
  ret = op_funcs->mluScaleYuvInit(&handle, width, height, dst_w,
                              dst_h, depth, ctx.src_pix_fmt.c_str());
  if (ret) {
    std::cout << "resize yuv op init failed" << std::endl;
    exit(1);
  }
  op_time = op_watcher.stop();
  std::cout << "init time:" << op_time << "ms" << std::endl;

  uint32_t src_y_elem_size = height * PAD_UP(width, ALIGN_Y_SCALE);
  uint32_t src_uv_elem_size = height / 2 * PAD_UP(width, ALIGN_Y_SCALE);
  uint32_t dst_y_elem_size = PAD_UP(dst_w, ALIGN_Y_SCALE) * dst_h;
  uint32_t dst_uv_elem_size = PAD_UP(dst_w, ALIGN_Y_SCALE) * dst_h / 2;
  uint32_t dst_elem_size = dst_y_elem_size + dst_uv_elem_size;

  void *src_y_mlu;
  void *src_uv_mlu;
  void *dst_y_mlu;
  void *dst_uv_mlu;
  cnrtMalloc((void **)(&src_y_mlu), src_y_elem_size);
  cnrtMalloc((void **)(&src_uv_mlu), src_uv_elem_size);
  cnrtMalloc((void **)(&dst_y_mlu), dst_y_elem_size);
  cnrtMalloc((void **)(&dst_uv_mlu), dst_uv_elem_size);
  cnrtMemcpy(src_y_mlu, input_cpu_yuv, src_y_elem_size,
             CNRT_MEM_TRANS_DIR_HOST2DEV);
  cnrtMemcpy(src_uv_mlu, (input_cpu_yuv + src_y_elem_size), src_uv_elem_size,
             CNRT_MEM_TRANS_DIR_HOST2DEV);
  cnrtMemcpy(src_y_mlu, input_cpu_yuv, src_y_elem_size,
            CNRT_MEM_TRANS_DIR_HOST2DEV);
  cnrtMemcpy(src_uv_mlu, (input_cpu_yuv + src_y_elem_size), src_uv_elem_size,
            CNRT_MEM_TRANS_DIR_HOST2DEV);

  op_time = 0.0;
  for (uint32_t i = 0; i < frame_num; i++) {
    op_watcher.start();
    if (strcmp(ctx.exec_mod.c_str(), "pad") == 0) {
      ret = op_funcs->mluScaleYuvExecPad(handle, src_y_mlu, src_uv_mlu,
                                      dst_y_mlu, dst_uv_mlu);
#if 0
    } else if (strcmp(ctx.exec_mod, "roi") == 0) {
      ret = op_funcs->mluScaleYuvExecCrop(handle,
                                src_y_mlu, src_uv_mlu, dst_y_mlu, dst_uv_mlu,
                                0, 0, 256, 256, 10, 10, 64, 64);
#endif
    } else {
      ret = op_funcs->mluScaleYuvExec(handle, src_y_mlu, src_uv_mlu,
                                      dst_y_mlu, dst_uv_mlu);
    }
    if (ret) {
      std::cout << "resize yuv exec failed" << std::endl;
      exit(-1);
    }
    op_time += op_watcher.stop();
  }
  std::cout << "exec time(ave.):" << op_time / frame_num
            << "ms, total frames:" << frame_num << std::endl;

  uint8_t *dst_yuv_cpu = (uint8_t *)malloc(dst_elem_size);
  cnrtMemcpy(dst_yuv_cpu, dst_y_mlu, dst_y_elem_size,
              CNRT_MEM_TRANS_DIR_DEV2HOST);
  cnrtMemcpy((dst_yuv_cpu + dst_y_elem_size), dst_uv_mlu, dst_uv_elem_size,
              CNRT_MEM_TRANS_DIR_DEV2HOST);

  op_time = 0.0;
  op_watcher.start();
  ret = op_funcs->mluScaleYuvDestroy(handle);
  if (ret) {
    std::cout << "resize yuv destroy failed" << std::endl;
    exit(-1);
  }
  op_time = op_watcher.stop();
  std::cout << "destroy time:" << op_time << "ms" << std::endl;

  if (save_flag) {
    cv::Mat dst_img;
    dst_img = cv::Mat(dst_h * 3/2, dst_w, CV_8UC1);
    memcpy(dst_img.data, dst_yuv_cpu, dst_w * dst_h * 3 /2);
    !strcmp(ctx.src_pix_fmt.c_str(), "nv12")?
      cv::cvtColor(dst_img, dst_img, cv::COLOR_YUV2BGR_NV12):
      cv::cvtColor(dst_img, dst_img, cv::COLOR_YUV2BGR_NV21);
    cv::imwrite(output_file, dst_img);
  }

  if (input_cpu_yuv) free(input_cpu_yuv);
  if (dst_yuv_cpu)   free(dst_yuv_cpu);
  if (src_y_mlu)     cnrtFree(src_y_mlu);
  if (src_uv_mlu)    cnrtFree(src_uv_mlu);
  if (dst_y_mlu)     cnrtFree(dst_y_mlu);
  if (dst_uv_mlu)    cnrtFree(dst_uv_mlu);

  return 0;
}
