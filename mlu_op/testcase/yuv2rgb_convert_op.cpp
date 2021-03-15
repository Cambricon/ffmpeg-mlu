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

#include "mluop.h"
#include "mluop_list.h"
#include "test_mluop.h"
#include <iostream>
#include <chrono>

#define PRINT_TIME 0

static int set_cnrt_ctx(unsigned int device_id, cnrtChannelType_t channel_id) {
  cnrtDev_t dev;
  cnrtRet_t ret;
  ret = cnrtGetDeviceHandle(&dev, device_id);
  CNRT_ERROR_CHECK(ret);
  ret = cnrtSetCurrentDevice(dev);
  CNRT_ERROR_CHECK(ret);
  if (channel_id >= CNRT_CHANNEL_TYPE_0) {
    ret = cnrtSetCurrentChannel(channel_id);
    CNRT_ERROR_CHECK(ret);
  }
  return 0;
}

void yuv2rgb_convert_op(void *ctx_, char **argv) {
  param_ctx_t *ctx = (param_ctx_t *)ctx_;
  ctx->algo = atoi(argv[1]);
  ctx->input_file = argv[2];
  ctx->width = atoi(argv[3]);
  ctx->height = atoi(argv[4]);
  ctx->src_pix_fmt = argv[5];
  ctx->dst_pix_fmt = argv[6];
  ctx->output_file = argv[7];
  ctx->frame_num = atoi(argv[8]);
  ctx->thread_num = atoi(argv[9]);
  ctx->save_flag = atoi(argv[10]);
  ctx->device_id = atoi(argv[11]);
  ctx->src_pix_chn_num = getPixFmtChannelNum(getCNPixFmtFromPixindex(ctx->src_pix_fmt));
  ctx->dst_pix_chn_num = getPixFmtChannelNum(getCNPixFmtFromPixindex(ctx->dst_pix_fmt));
  ctx->depth_size = 1; // depth_size: 1->uint8, 2->f16, 4->f32

  if (ctx->algo <= 0) ctx->algo = 0;
  if (ctx->save_flag <= 0) ctx->save_flag = 0;
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
    ret = pthread_create(&tids[i], &attr, process_yuv2rgb_op, (void *)ctx);
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

void *process_yuv2rgb_op(void *ctx_) {
  param_ctx_t *ctx = (param_ctx_t *)ctx_; 
  uint32_t width = ctx->width;
  uint32_t height =  ctx->height;
  uint32_t frame_num = ctx->frame_num;
  uint32_t device_id = ctx->device_id;
  uint32_t save_flag = ctx->save_flag;
  uint32_t depth_size = ctx->depth_size;
  uint32_t src_pix_chn_num = ctx->src_pix_chn_num;
  uint32_t dst_pix_chn_num = ctx->dst_pix_chn_num;
  const char *src_pix_fmt = ctx->src_pix_fmt;
  const char *dst_pix_fmt = ctx->dst_pix_fmt;
  const char *input_file = ctx->input_file;
  const char *output_file =ctx->output_file;

  HANDLE handle;
  #if PRINT_TIME
  float time_use = 0;
  struct timeval end;
  struct timeval start;
  gettimeofday(&start, NULL);
  #endif
  /*--------init op--------*/
  set_cnrt_ctx(device_id, CNRT_CHANNEL_TYPE_NONE);
  mluop_convert_yuv2rgb_invoke_init(&handle,
                                    width,
                                    height,
                                    getCNPixFmtFromPixindex(src_pix_fmt),
                                    getCNPixFmtFromPixindex(dst_pix_fmt),
                                    getSizeFromDepth(depth_size));
  #if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[init] time: %.3f ms\n", time_use/1000);
  #endif
  /*----------------------prepare_input_data-------------------------*/
  uint32_t src_y_elem_size = height * width * depth_size * src_pix_chn_num;
  uint32_t src_uv_elem_size = height / 2 * width * depth_size * src_pix_chn_num;
  uint32_t src_elem_size = src_y_elem_size + src_uv_elem_size;
  uint32_t dst_elem_size = height * width * depth_size * dst_pix_chn_num;

  //read image to src_cpu
  uint8_t *src_cpu = (uint8_t *)malloc(src_elem_size);
  uint8_t *dst_cpu = (uint8_t *)malloc(dst_elem_size);

  FILE *fp = fopen(input_file, "rb");
  if (fp == NULL) {
    printf("Error opening input image for write \n");
  }
  size_t wt_;
  wt_ = fread(src_cpu, 1, src_elem_size, fp);
  if (wt_ != src_elem_size) {
    printf("write data fail\n");
  }
  fclose(fp);
  fp = NULL;
  void *src_y_mlu;
  void *src_uv_mlu;
  void *dst_mlu;
  cnrtMalloc((void **)(&src_y_mlu), src_y_elem_size);
  cnrtMalloc((void **)(&src_uv_mlu), src_uv_elem_size);
  cnrtMemcpy(src_y_mlu, src_cpu, src_y_elem_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  cnrtMemcpy(src_uv_mlu, (src_cpu + src_y_elem_size), src_uv_elem_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  cnrtMalloc((void **)(&dst_mlu), dst_elem_size);

  #if PRINT_TIME
  gettimeofday(&start, NULL);
  #endif
  /*-------execute op-------*/
  for (uint32_t i = 0; i < frame_num; i++) {
    mluop_convert_yuv2rgb_invoke_exec(handle, src_y_mlu, src_uv_mlu, dst_mlu);
    // printf("exec successfully\n");
  }
  cnrtMemcpy(dst_cpu, dst_mlu, dst_elem_size, CNRT_MEM_TRANS_DIR_DEV2HOST);
  #if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[exec] time(ave.): %.3f ms, total frame: %d\n", (time_use/1000.0)/frame_num, frame_num);
  #endif
  /*-----------------------save file----------------------*/
  if (save_flag){
      //prepare_output_data(handle, output_file);
    FILE *fp = fopen(output_file, "wb");
    if (fp == NULL) {
      printf("Error opening output image for write \n");
    }
    size_t written_;
    // host: dump result to file
      written_ = fwrite(dst_cpu, 1, dst_elem_size, fp);
      if (written_ != dst_elem_size) {
        printf("Error writting rgb data \n");
      }
    fclose(fp);
    fp = NULL;
  }
  #if PRINT_TIME
  gettimeofday(&start, NULL);
  #endif
  /*-------destroy op-------*/
  mluop_convert_yuv2rgb_invoke_destroy(handle);

  #if PRINT_TIME  
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[destroy] time: %.3f ms\n", time_use/1000);
  #endif

  if (src_cpu)
    free(src_cpu);
  if (src_y_mlu)
    cnrtFree(src_y_mlu);
  if (src_uv_mlu)
    cnrtFree(src_uv_mlu);
  if (dst_cpu)
    free(dst_cpu);
  if (dst_mlu)
    cnrtFree(dst_mlu);
  return NULL;
}
