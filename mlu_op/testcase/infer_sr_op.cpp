/*************************************************************************
 * Copyright (C) [2019-2022] by Cambricon, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *************************************************************************/
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include "mluop.h"
#include "mluop_list.h"
#include "test_mluop.h"
#include <iostream>
#include <chrono>

using namespace std;
using namespace cv;

#define PRINT_TIME 0

void infer_sr_op(void *ctx_, char **argv) {
  param_ctx_t *ctx = (param_ctx_t *)ctx_;
  ctx->algo = atoi(argv[1]);
  ctx->input_file = argv[2];
  ctx->model_path = argv[3];
  ctx->device_id = atoi(argv[4]);
  ctx->dev_channel = atoi(argv[5]);
  ctx->is_rgb = atoi(argv[6]);
  ctx->frame_num = atoi(argv[7]);
  ctx->thread_num = atoi(argv[8]);
  if (ctx->algo <= 0) ctx->algo = 0;
  if (ctx->frame_num <= 0) ctx->frame_num = 10;
  if (ctx->thread_num <= 0) ctx->thread_num = THREADS_NUM;

  int ret = 0;
  void *status = NULL;
  pthread_t tids[THREADS_NUM];
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  for (uint32_t i = 0; i < ctx->thread_num; i++) {
    printf("create thread [%d]\n", i);
    ret = pthread_create(&tids[i], &attr, process_sr_op, (void *)ctx);
  }

  pthread_attr_destroy(&attr);
  for (uint32_t i = 0; i < ctx->thread_num; i++) {
    ret = pthread_join(tids[i], &status);
    if (ret != 0){
        printf("pthread_join error(thread id :%lu): error_code=%d\n",(long unsigned)tids[i], ret);
    } else {
        printf("pthread_join ok(thread id :%lu): get status:=%ld\n",(long unsigned)tids[i], (long)status);
    }
  }
}

void *process_sr_op(void *ctx_) {
  param_ctx_t *ctx = (param_ctx_t *)ctx_;
  const char *model_path = ctx->model_path;
  uint32_t device_id = ctx->device_id;
  uint32_t dev_channel = ctx->dev_channel;
  uint32_t frame_num = ctx->frame_num;
  const char *input_file = ctx->input_file;

  HANDLE handle;
  #if PRINT_TIME
  float time_use = 0;
  struct timeval end;
  struct timeval start;
  gettimeofday(&start, NULL);
  #endif
  /*--------init op--------*/
  mluop_infer_sr_init(&handle, model_path, device_id, dev_channel);
  #if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[init] time: %.3f ms\n", time_use/1000);
  #endif
  /*--------prepare input data-------*/
  cv::Mat Im_tmp = cv::imread(input_file);
  cv::Mat Im;
  cv::resize(Im_tmp, Im, cv::Size(640, 360));

  uint32_t dst_elem_size = 1280 * 720 *3;
  void *data_in;
  void *data_out = (uint8_t *)malloc(dst_elem_size);
  memset(data_out, 0, dst_elem_size);

  data_in = Im.data;

  uint32_t in_linesize = 640 * 3;
  uint32_t out_linesize = 1280 *3;
  #if PRINT_TIME
  gettimeofday(&start, NULL);
  #endif
  /*-------execute op-------*/
  for (uint32_t i = 0; i < frame_num; i++) {
     mluop_infer_sr_exec(handle, data_in, in_linesize, out_linesize, data_out);
  }
  #if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[exec] time(ave.): %.3f ms, total frame: %d\n", (time_use/1000.0)/frame_num, frame_num);
  #endif

  #if PRINT_TIME
  gettimeofday(&start, NULL);
  #endif

  cv::Mat out_img = cv::Mat(720, 1280, CV_8UC3, data_out);
  cv::imwrite("./out_img.png", out_img);
  /*-------destroy op-------*/
  mluop_infer_sr_destroy(handle);

  #if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[destroy] time: %.3f ms\n", time_use/1000);
  #endif

  // free(data_out);
  out_img.release();
  return NULL;
}
