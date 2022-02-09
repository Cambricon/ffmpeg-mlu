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
#include "cnrt.h"
#include "mluop.h"
#include "mluop_list.h"
#include "test_mluop.h"

int main(int argc, char **argv) {
  cnrtRet_t cnrt_ret;
  cnrt_ret = cnrtInit(0);
  CNRT_ERROR_CHECK(cnrt_ret);
  if (argc < 5) {
    printf(" ==================================================================================\n");
    printf("|                               description                                        |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|./test mluop <algo> :args for choosing function                                   |\n"
           "|  after choosing algo, you can type other args following the description          |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|Algo list:                                                                        |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|[0] <--> process_resize_yuv                                                       |\n");
    printf("|                                                                                  |\n");
    printf("|./test mluop <algo> <src_file> <src_width> <src_height> <dst_width> <dst_height>  |\n"
           "|  <dst_file> <pixfmt> <frame_num> <thread_num> <save_flag> <device_id>            |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|[1] <--> process_resize_rgbx                                                      |\n");
    printf("|                                                                                  |\n");
    printf("|./test mluop <algo> <src_file> <src_width> <src_height> <dst_width> <dst_height>  |\n"
           "| <dst_file> <pixfmt> <frame_num> <thread_num> <save_flag> <device_id>             |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|[2] <--> process_convert_yuvrgbx                                                  |\n");
    printf("|                                                                                  |\n");
    printf("|./test mluop <algo> <src_file> <width> <height> <dst_file> <src_pixfmt>           |\n"
           "| <dst_pixfmt> <frame_num> <thread_num> <save_flag> <device_id>                    |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|[3] <--> process_convert_rgbx2yuv                                                 |\n");
    printf("|                                                                                  |\n");
    printf("|./test mluop <algo> <src_file> <width> <height> <dst_file> <src_pixfmt>           |\n"
           "| <dst_pixfmt> <frame_num> <thread_num> <save_flag> <device_id>                    |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|[4] <--> process_convert_rgbx2rgbx                                                |\n");
    printf("|                                                                                  |\n");
    printf("|./test mluop <algo> <src_file> <src_width> <src_height> <dst_file>                |\n"
           "| <src_pix_fmt> <dst_pixfmt> <frame_num> <thread_num> <save_flag> <device_id>      |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|[5] <--> process_resize_cvt_yuv2rgbx                                              |\n");
    printf("|                                                                                  |\n");
    printf("|./test mluop <algo> <src_file> <src_width> <src_height> <dst_w> <dst_h> <dst_file>|\n"
           "| <src_pixfmt> <dst_pixfmt> <frame_num> <thread_num> <save_flag> <dev_id>          |\n");
    printf(" ================================================================================== \n");
    return 1;
  }

  param_ctx_t *ctx = malloc(sizeof(param_ctx_t));
  memset(ctx, 0, sizeof(param_ctx_t));

  ctx->algo = atoi(argv[1]);
  if (0 == ctx->algo) {
    printf("exec algo[%d]: process_resize_yuv \n", ctx->algo);
    yuv2yuv_resize_op(ctx, argv);
  } else if (1 == ctx->algo) {
    printf("exec algo[%d]: process_resize_rgbx \n", ctx->algo);
    rgbx2rgbx_resize_op(ctx, argv);
  } else if (2 == ctx->algo) {
    printf("exec algo[%d]: process_convert_yuvrgbx \n", ctx->algo);
    yuv2rgbx_convert_op(ctx, argv);
  } else if (3 == ctx->algo) {
    printf("exec algo[%d]: process_convert_rgbx2yuv \n", ctx->algo);
    rgbx2yuv_convert_op(ctx, argv);
  } else if (4 == ctx->algo) {
    printf("exec algo[%d]: process_convert_rgbx \n", ctx->algo);
    Rgbx2RgbxConvertOp(ctx, argv);
  } else if (5 == ctx->algo) {
    printf("exec algo[%d]: ProcessResizeCvtYuv2Rgbx \n", ctx->algo);
    Yuv2RgbxResizeCvtOp(ctx, argv);
  } else {
    printf("don't support algo ...\n");
  }

  free(ctx);
  cnrtDestroy();
  printf("program exiting.\n");

  return 0;
}
