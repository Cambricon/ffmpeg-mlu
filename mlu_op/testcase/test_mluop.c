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
    printf("|[0] <--> process_resize_invoke_yuv                                                |\n");
    printf("|                                                                                  |\n");
    printf("|./test mluop <algo> <src_file> <src_width> <src_height> <dst_width> <dst_height>  |\n"
           "| <dst_file> <pixfmt> <frame_num> <thread_num> <save_flag> <device_id>             |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|[1] <--> process_resize_invoke_rgbx                                               |\n");
    printf("|                                                                                  |\n");
    printf("|./test mluop <algo> <src_file> <src_width> <src_height> <dst_width> <dst_height>  |\n"
           "| <dst_file> <pixfmt> <frame_num> <thread_num> <save_flag> <device_id>             |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|[2] <--> process_convert_invoke_yuv_to_rgbx                                       |\n");
    printf("|                                                                                  |\n");
    printf("|./test mluop <algo> <src_file> <width> <height> <src_pixfmt> <dst_pixfmt>         |\n"
           "| <dst_file> <frame_num> <thread_num> <save_flag> <device_id>                      |\n");
    printf("|----------------------------------------------------------------------------------|\n");
    printf("|[3] <--> process_infer_src                                                        |\n");
    printf("|                                                                                  |\n");
    printf("|./test mluop <algo> <src_file> <model_path>                                       |\n"
           "| <dev_id> <dev_channel> <is_rgb> <frame_num> <thread_num>                         |\n");
    printf(" ================================================================================== \n");

    return 1;
  }

  param_ctx_t *ctx = malloc(sizeof(param_ctx_t));
  memset(ctx, 0, sizeof(param_ctx_t));

  ctx->algo = atoi(argv[1]);
  if (0 == ctx->algo) {
    printf("exec algo[%d]: process_resize_invoke_yuv \n", ctx->algo);
    yuv2yuv_resize_op(ctx, argv);
  } else if (1 == ctx->algo) {
    printf("exec algo[%d]: process_resize_invoke_rgbx \n", ctx->algo);
    rgbx2rgbx_resize_op(ctx, argv);
  }else if (2 == ctx->algo) {
    printf("exec algo[%d]: process_convert_invoke_yuv_to_rgbx \n", ctx->algo);
    yuv2rgb_convert_op(ctx, argv);
  } else if (3 == ctx->algo) {
    printf("exec algo[%d]: process_infer_src \n", ctx->algo);
    infer_sr_op(ctx, argv);
  } else {
    printf("don't support algo ...\n");
  }

  free(ctx);
  cnrtDestroy();
  printf("program exiting.\n");

  return 0;
}
