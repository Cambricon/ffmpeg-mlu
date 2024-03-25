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
#include <iostream>
#include "cnrt.h"
#include "mluop_context.hpp"

void usage(void) {
  printf("./test mluop input.conf\n");
  exit(-1);
}

extern void yuv2yuv_resize_op(params_conf &op_conf);
extern void rgbx2rgbx_resize_op(params_conf &op_conf);
extern void yuv2rgbx_convert_op(params_conf &op_conf);
extern void rgbx2yuv_convert_op(params_conf &op_conf);
extern void rgbx2rgbx_convert_op(params_conf &op_conf);
extern void yuv2rgbx_resize_cvt_op(params_conf &op_conf);
extern void overlay_op(params_conf &op_conf);

inline int set_test_algo(mluOpMember algo, params_conf op_conf) {
  switch (algo) {
  case RESIZE_YUV:
    std::cout << "=============================================" << std::endl;
    std::cout << "Test resize_yuv op:" << std::endl;
    yuv2yuv_resize_op(op_conf);
    break;
  case RESIZE_RGBX:
    std::cout << "=============================================" << std::endl;
    std::cout << "Test resize_rgbx op:" << std::endl;
    rgbx2rgbx_resize_op(op_conf);
    break;
  case CONVERT_YUV2RGBX:
    std::cout << "=============================================" << std::endl;
    std::cout << "Test convert_yuvrgbx op:" << std::endl;
    yuv2rgbx_convert_op(op_conf);
    break;
  case CONVERT_RGBX2YUV:
    std::cout << "=============================================" << std::endl;
    std::cout << "Test convert_rgbx2yuv op:" << std::endl;
    rgbx2yuv_convert_op(op_conf);
    break;
  case CONVERT_RGBX:
    std::cout << "=============================================" << std::endl;
    std::cout << "Test convert_rgbx op:" << std::endl;
    rgbx2rgbx_convert_op(op_conf);
    break;
  case RESIZE_CONVERT_YUV2RGBX:
    std::cout << "=============================================" << std::endl;
    std::cout << "Test resize_convert_yuv2rgbx op:" << std::endl;
    yuv2rgbx_resize_cvt_op(op_conf);
    break;
  case OVERLAY:
    std::cout << "=============================================" << std::endl;
    std::cout << "Test overlay_op op:" << std::endl;
    overlay_op(op_conf);
    break;
  default:
    std::cout << "=============================================" << std::endl;
    std::cout << "Unknow test op name" << std::endl;
    break;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 1) {
    usage();
    return -1;
  }

  param_ctx_t *ctx = (param_ctx_t *)malloc(sizeof(param_ctx_t));
  memset(ctx, 0, sizeof(param_ctx_t));

#if CNRT_MAJOR_VERSION < 5
  MLUOP_TEST_CHECK(cnrtInit(0));
#endif

  int ret = 0;
  int algo_num = 0;
  std::string input = argv[1];

  // params_conf op_conf;
  std::map<std::string, params_conf> params;
  std::unordered_map<std::string, mluOpMember> algo_type_map = {
    {"resize_yuv", RESIZE_YUV},
    {"resize_rgbx", RESIZE_RGBX},
    {"convert_yuv2rgbx", CONVERT_YUV2RGBX},
    {"convert_rgbx2yuv", CONVERT_RGBX2YUV},
    {"convert_rgbx", CONVERT_RGBX},
    {"resize_convert_yuv2rgbx", RESIZE_CONVERT_YUV2RGBX},
    {"overlay", OVERLAY},
  };

  parserTool parser(input);
  algo_num = parser.get_conf_params(params);

  std::cout << "params num:" << params.size() << std::endl;
  std::map<std::string, params_conf>::iterator iter;
  for (iter = params.begin(); iter != params.end(); iter++) {
    auto search = algo_type_map.find(parser.get_algo_name(iter->first));
    if (search == algo_type_map.end()) {
      std::cout << "unknow algo name: " << iter->first << std::endl;
      continue;
    }
    set_test_algo(search->second, iter->second);
  }

  free(ctx);
#if CNRT_MAJOR_VERSION < 5
  cnrtDestroy();
#endif

  std::cout << "=============================================" << std::endl;
  std::cout << "test mluop done" << std::endl;

  return 0;
}
