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

#include "mluop.h"
#include "mluop_list.h"
#include "test_mluop.h"
#include <iostream>

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

void rgbx2rgbx_resize_op(void *ctx_, char **argv) {
  param_ctx_t *ctx = (param_ctx_t *)ctx_;
  ctx->algo = atoi(argv[1]);
  ctx->input_file = argv[2];
  ctx->src_w = atoi(argv[3]);
  ctx->src_h = atoi(argv[4]);
  ctx->dst_w = atoi(argv[5]);
  ctx->dst_h = atoi(argv[6]);
  ctx->output_file = argv[7];
  ctx->pix_fmt = argv[8];
  ctx->frame_num = atoi(argv[9]);
  ctx->thread_num = atoi(argv[10]);
  ctx->save_flag = atoi(argv[11]);
  ctx->device_id = atoi(argv[12]);
  ctx->pix_chn_num = getPixFmtChannelNum(getCNPixFmtFromPixindex(ctx->pix_fmt));
  ctx->depth_size = 1; // depth_size: 1->uint8, 2->f16, 4->f32

  if (ctx->algo <= 0) ctx->algo = 0;
  if (ctx->dst_w <= 0) ctx->dst_w = 352;
  if (ctx->dst_h <= 0) ctx->dst_h = 288;
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
    // printf("create thead [%d]\n", i);
    ret = pthread_create(&tids[i], &attr, process_resize_invoke_rgbx, (void *)ctx);
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

void *process_resize_invoke_rgbx(void *ctx_) {
  param_ctx_t *ctx = (param_ctx_t *)ctx_; 
  bool save_flag = ctx->save_flag;
  uint32_t input_w = ctx->src_w;
  uint32_t input_h =  ctx->src_h;
  uint32_t dst_w = ctx->dst_w;
  uint32_t dst_h = ctx->dst_h;
  uint32_t frame_num = ctx->frame_num;
  uint32_t depth_size = ctx->depth_size;
  uint32_t pix_chn_num = ctx->pix_chn_num;
  uint32_t device_id = ctx->device_id;
  const char *pix_fmt = ctx->pix_fmt;
  // uint32_t chn_idx = ctx->chn_idx;

  int batch_size = 1;
  const char *filename = ctx->input_file;
  const char *output_file =ctx->output_file;

  HANDLE handle;
  set_cnrt_ctx(device_id, CNRT_CHANNEL_TYPE_NONE /* CNRT_CHANNEL_TYPE_0 */);

  /*-------------------init op------------------*/
  mluop_resize_rgbx_invoke_init(&handle,
                                input_w,
                                input_h,
                                dst_w,
                                dst_h,
                                getCNPixFmtFromPixindex(pix_fmt),
                                getSizeFromDepth(depth_size));
  // prepare src size
  uint32_t src_stride = input_w * pix_chn_num * depth_size;
  uint32_t src_size = input_h * src_stride;
  void *src_cpu = (void *)malloc(src_size);

  uint32_t dst_stride = dst_w * pix_chn_num * depth_size;
  uint32_t dst_size = dst_h * dst_stride;
  void *dst_cpu = (void *)malloc(dst_size);

  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    printf("Error opening input image for write \n");
    return NULL;
  }
  // uint32_t ss = ftell(fp);
  size_t wt_;
  wt_ = fread(src_cpu, 1, src_size, fp);
  if (wt_ != src_size) {
    // printf("Error reading rgb data, file size: %d, wt size: %d, dst size: %d \n", ss, wt_, src_size);
    return NULL;
  }
  fclose(fp);
  fp = NULL;

  void *src_mlu;
  void *dst_mlu;
  CNRT_CHECK(cnrtMalloc((void **)(&src_mlu), depth_size * src_size));
  CNRT_CHECK(cnrtMemcpy(src_mlu, src_cpu, src_size, CNRT_MEM_TRANS_DIR_HOST2DEV));
  CNRT_CHECK(cnrtMalloc((void **)(&dst_mlu), depth_size * dst_size));

  uint32_t d_x = 0, d_y = 0, d_w = dst_w, d_h = dst_h;
  for (uint32_t i = 0; i < frame_num; i++) {
    mluop_resize_rgbx_invoke_exec(handle, src_mlu, dst_mlu, d_x, d_y, d_w, d_h);
  }

  CNRT_CHECK(cnrtMemcpy(dst_cpu, dst_mlu, dst_size, CNRT_MEM_TRANS_DIR_DEV2HOST));

  /*-------destroy op-------*/
  mluop_resize_rgbx_invoke_destroy(handle);
  /*-------sace file-------*/
  if (save_flag) {
    FILE *fp = fopen(output_file, "wb");
    if (fp == NULL) {
      printf("Error opening output image for write \n");
      return NULL;
    }
    size_t written_;
    written_ = fwrite(dst_cpu, 1, dst_size, fp);
    if (written_ != dst_size) {
      printf("Error writting rgb data \n");
      return NULL;
    }
    fclose(fp);
    fp = NULL;
  }


  if (src_cpu)
    free(src_cpu);
  if (src_mlu)
    cnrtFree(src_mlu);
  if (dst_cpu)
    free(dst_cpu);
  if (dst_mlu)
    cnrtFree(dst_mlu);
  return NULL;
}
