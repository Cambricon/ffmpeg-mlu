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

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "mluop.h"
#include "mluop_list.h"
#include "test_mluop.h"

#define CV_8U 0
#define CV_CN_SHIFT 3
#define CV_DEPTH_MAX (1 << CV_CN_SHIFT)
#define CV_MAT_DEPTH_MASK (CV_DEPTH_MAX - 1)
#define CV_MAT_DEPTH(flags) ((flags)&CV_MAT_DEPTH_MASK)
#define CV_MAKETYPE(depth, cn) (CV_MAT_DEPTH(depth) + (((cn)-1) << CV_CN_SHIFT))
#define CV_8UC3 CV_MAKETYPE(CV_8U, 3)
#define CV_8UC(n) CV_MAKETYPE(CV_8U, (n))

extern void BGR2YUV420P(cv::Mat src, cv::Mat &dst, const char *pix_fmt);

void Yuv2RgbxResizeCvtOp(void *ctx_, char **argv) {
  param_ctx_t *ctx = (param_ctx_t *)ctx_;
  ctx->algo = atoi(argv[1]);
  ctx->input_file = argv[2];
  ctx->src_w = atoi(argv[3]);
  ctx->src_h = atoi(argv[4]);
  ctx->dst_w = atoi(argv[5]);
  ctx->dst_h = atoi(argv[6]);
  ctx->output_file = argv[7];
  ctx->src_pix_fmt = argv[8];
  ctx->dst_pix_fmt = argv[9];
  ctx->frame_num = atoi(argv[10]);
  ctx->thread_num = atoi(argv[11]);
  ctx->save_flag = atoi(argv[12]);
  ctx->device_id = atoi(argv[13]);
  ctx->depth_size = 1;  // depth_size: 1->uint8, 2->f16, 4->f32

  char depth_[3] = "8U";
  ctx->depth = depth_;

  if (ctx->algo <= 0) ctx->algo = 2;
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
    printf("create thead [%d]\n", i);
    ret =
        pthread_create(&tids[i], &attr, ProcessResizeCvtYuv2Rgbx, (void *)ctx);
  }

  pthread_attr_destroy(&attr);
  for (uint32_t i = 0; i < ctx->thread_num; i++) {
    ret = pthread_join(tids[i], &status);
    if (ret != 0) {
      printf("pthread_join error(thread id :%lu): error_code=%d\n",
             (long unsigned)tids[i], ret);
    } else {
      printf("pthread_join ok(thread id :%lu): get status:=%ld\n",
             (long unsigned)tids[i], (long)status);
    }
  }
}

void *ProcessResizeCvtYuv2Rgbx(void *ctx_) {
  param_ctx_t *ctx = (param_ctx_t *)ctx_;
  bool save_flag = ctx->save_flag;
  uint32_t src_w = ctx->src_w;
  uint32_t src_h = ctx->src_h;
  uint32_t dst_w = ctx->dst_w;
  uint32_t dst_h = ctx->dst_h;
  uint32_t frame_num = ctx->frame_num;
  uint32_t device_id = ctx->device_id;
  uint32_t dst_pix_chn_num =
      getPixFmtChannelNum(getCNCVPixFmtFromPixindex(ctx->dst_pix_fmt));
  uint32_t depth_size = ctx->depth_size;
  const char *depth = ctx->depth;
  const char *filename = ctx->input_file;
  const char *output_file = ctx->output_file;

  set_cnrt_ctx(device_id, CNRT_CHANNEL_TYPE_NONE /* CNRT_CHANNEL_TYPE_0 */);

  uint32_t src_y_stride = PAD_UP(src_w, ALIGN_RESIZE_CVT) * depth_size;
  uint32_t src_uv_stride = PAD_UP(src_w, ALIGN_RESIZE_CVT) * depth_size;
  uint32_t dst_stride =
      PAD_UP(dst_w, ALIGN_RESIZE_CVT) * dst_pix_chn_num * depth_size;
  uint32_t src_y_size = src_h * src_y_stride;
  uint32_t src_uv_size = src_h * src_uv_stride * 3 / 2;
  uint32_t src_size = src_y_size + src_uv_size;
  uint32_t dst_size = dst_h * dst_stride;

  uint8_t *src_cpu = (uint8_t *)malloc(src_size);
  uint8_t *dst_cpu = (uint8_t *)malloc(dst_size);

  cv::Mat src_mat;
  cv::Mat src_yuv_mat;
  cv::Mat dst_mat;
  src_mat = cv::imread(filename, cv::IMREAD_COLOR);  // read 8U_C3 BGR
  BGR2YUV420P(src_mat, src_yuv_mat, ctx->src_pix_fmt);
  // plane Y
  for (uint32_t row = 0; row < src_h; ++row) {
    memcpy(reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(src_cpu) +
                                       row * src_y_stride),
           src_yuv_mat.ptr<uint8_t>(row),
           src_yuv_mat.cols * src_yuv_mat.elemSize());
  }
  // plane UV
  for (uint32_t row = 0; row < src_h / 2; ++row) {
    memcpy(reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(src_cpu) +
                                       src_y_size + row * src_uv_stride),
           src_yuv_mat.ptr<uint8_t>(row + src_h),
           src_yuv_mat.cols * src_yuv_mat.elemSize());
  }

  void *src_y_mlu;
  void *src_uv_mlu;
  void *dst_mlu;
  cnrtMalloc((void **)(&src_y_mlu), src_y_size);
  cnrtMalloc((void **)(&src_uv_mlu), src_uv_size);
  cnrtMalloc((void **)(&dst_mlu), dst_size);
  cnrtMemcpy(src_y_mlu, src_cpu, src_y_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  cnrtMemcpy(src_uv_mlu, (src_cpu + src_y_size), src_uv_size,
             CNRT_MEM_TRANS_DIR_HOST2DEV);

  HANDLE handle;
#if PRINT_TIME
  float time_use = 0;
  struct timeval end;
  struct timeval start;
  gettimeofday(&start, NULL);
#endif
  /*--------init op--------*/
  MluopResizeCvtInit(&handle, src_w, src_h, dst_w, dst_h, ctx->src_pix_fmt,
                     ctx->dst_pix_fmt, depth);
#if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use =
      (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[init] time: %.3f ms\n", time_use / 1000);
#endif

  /*-------execute op-------*/
  for (uint32_t i = 0; i < frame_num; i++) {
    cnrtMemcpy(src_y_mlu, src_cpu, src_y_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
    cnrtMemcpy(src_uv_mlu, (src_cpu + src_y_size), src_uv_size,
               CNRT_MEM_TRANS_DIR_HOST2DEV);
#if PRINT_TIME
    gettimeofday(&start, NULL);
#endif
    MluopResizeCvtExec(handle, src_y_mlu, src_uv_mlu, dst_mlu);
#if PRINT_TIME
    gettimeofday(&end, NULL);
    time_use =
        (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
    printf("[exec] time(ave.): %.3f ms, total frame: %d\n",
           (time_use / 1000.0) / 1.0, i);
#endif
    /*----------D2H-----------*/
    cnrtMemcpy(dst_cpu, dst_mlu, dst_size, CNRT_MEM_TRANS_DIR_DEV2HOST);
  }
#if PRINT_TIME
  gettimeofday(&start, NULL);
#endif
  /*-------destroy op-------*/
  MluopResizeCvtDestroy(handle);
#if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use =
      (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[destroy] time: %.3f ms\n", time_use / 1000);
#endif
  /*-------sace file-------*/
  if (save_flag) {
    dst_mat.create(dst_h, dst_w, CV_8UC(dst_pix_chn_num));
    for (uint32_t row = 0; row < dst_h; ++row) {
      memcpy(dst_mat.ptr<uint8_t>(row),
             reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(dst_cpu) +
                                         row * dst_stride),
             dst_mat.cols * dst_mat.elemSize());  // valid data len = pic width
                                                  // * size of each element
    }
    cv::imwrite(output_file, dst_mat);
  }
  if (src_cpu) free(src_cpu);
  if (src_y_mlu) cnrtFree(src_y_mlu);
  if (src_uv_mlu) cnrtFree(src_uv_mlu);
  if (dst_cpu) free(dst_cpu);
  if (dst_mlu) cnrtFree(dst_mlu);
  return NULL;
}
