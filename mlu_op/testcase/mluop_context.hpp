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
#ifndef __MLUOP_CONTEXT_H__
#define __MLUOP_CONTEXT_H__

#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <sys/time.h>
#include <chrono>
#include <opencv2/opencv.hpp>

#include "cnrt.h"
#include "utils.hpp"

#if CNRT_MAJOR_VERSION < 5
#define MLUOP_TEST_CHECK(ret)                                                 \
  if (ret != CNRT_RET_SUCCESS) {                                              \
    fprintf(stderr, "error occur, func: %s, line: %d\n", __func__, __LINE__); \
    return -1;                                                                \
  }
#else
#define MLUOP_TEST_CHECK(ret)                                                 \
  if (ret != cnrtSuccess) {                                                   \
    fprintf(stderr, "error occur, func: %s, line: %d\n", __func__, __LINE__); \
    return -1;                                                                \
  }
#endif
typedef void *HANDLE;

typedef struct {
  int algo;
  bool save_flag;
  uint32_t src_w;
  uint32_t src_h;
  uint32_t dst_w;
  uint32_t dst_h;
  uint32_t frame_num;
  uint32_t thread_num;
  uint32_t device_id;
  uint32_t src_roi_x;
  uint32_t src_roi_y;
  uint32_t dst_roi_x;
  uint32_t dst_roi_y;
  std::string input_file;
  std::string output_file;
  std::string exec_mod;
  std::string src_pix_fmt;
  std::string dst_pix_fmt;
} param_ctx_t;

typedef enum mluOpMember {
  RESIZE_YUV = 0,
  RESIZE_RGBX,
  CONVERT_YUV2RGBX,
  CONVERT_RGBX2YUV,
  CONVERT_RGBX,
  RESIZE_CONVERT_YUV2RGBX,
  UNKNOW_MEMBER,
} mluOpMember;

struct roiRect {
  int x;
  int y;
  int w;
  int h;
};

typedef class timeWatch {
public:
  void start() {
    m_time = std::chrono::high_resolution_clock::now();
  }
  double stop() {
    double duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::high_resolution_clock::now().time_since_epoch() -
              m_time.time_since_epoch()).count() / 1.0e6;
    return duration;
  }

private:
  std::chrono::high_resolution_clock::time_point m_time;
};

using params_conf = std::map<std::string, std::string>;
class parserTool {
public:
  parserTool(std::string inputfile):m_inputfile(inputfile) {}
  virtual ~parserTool() {}

private:
  std::string m_inputfile;
  std::map<std::string, params_conf> m_algos_conf;

public:
  int get_conf_params(std::map<std::string, params_conf> &algo_conf) {
    int ret;
    ret = parser_conf_file();
    std::cout << "Paser:" << m_inputfile
              << ", total op num:" << ret << std::endl;
    algo_conf.insert(m_algos_conf.begin(), m_algos_conf.end());
    return ret;
  }

private:
  std::string trim_str(std::string &str) {
    std::string res = str;
    if (res.empty()) return res;
    res.erase(0, res.find_first_not_of(" "));
    res.erase(res.find_last_not_of(" ") + 1);
    return res;
  }
  std::vector<std::string>
    split_string_stream(std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream token_stream(s);
    while (std::getline(token_stream, token, delimiter)) {
      tokens.push_back(trim_str(token));
    }
    return tokens;
  }
  int parser_conf_file() {
    std::string line_str;
    std::string algo_name;
    params_conf algo_params;
    int line_num = 0;
    int conf_start = 1;

    std::ifstream parser;
    parser.open(m_inputfile.c_str(), std::ios::in);
    if (!parser.is_open()) {
      printf("Unable to open config file\n");
      return -1;
    }
    while (std::getline(parser, line_str)) {
      if (line_str.size() == 0 || line_str[0] == '#' || line_str[0] == '{') {
        continue;
      } else if (line_str[0] == '[') {
        algo_name = line_str.substr(1, line_str.length() - 2);
        continue;
      } else if (line_str[0] == '}') {
        line_num++;
        m_algos_conf.insert(std::make_pair(algo_name, algo_params));
        algo_params.clear();
        continue;
      } else {
        std::vector<std::string> tokens = split_string_stream(line_str, ':');
        algo_params.insert(std::make_pair(tokens[0], tokens[1]));
        tokens.clear();
      }
    }
    parser.close();
    return line_num;
  }
};

static int set_cnrt_ctx(unsigned int device_id) {
#if CNRT_MAJOR_VERSION < 5
  cnrtDev_t dev;
  MLUOP_TEST_CHECK(cnrtGetDeviceHandle(&dev, device_id));
  MLUOP_TEST_CHECK(cnrtSetCurrentDevice(dev));
  cnrtDeviceInfo_t info;
  cnrtGetDeviceInfo(&info, device_id);
  std::cout << "dev id:" << device_id << ", dev name:" << info.device_name;
  std::cout << std::endl;
#else
  cnrtDeviceProp_t prop;
  MLUOP_TEST_CHECK(cnrtSetDevice(device_id));
  MLUOP_TEST_CHECK(cnrtGetDeviceProperties(&prop, device_id));
  std::cout << "dev id:" << device_id << ", dev name:" << prop.name;
  std::cout << std::endl;
#endif

  return 0;
}

#define CNAPI
typedef int CNAPI mluCvtRgbx2RgbxInit_t(void**, int, int, const char*, const char*, const char*);
typedef int CNAPI mluCvtRgbx2RgbxExec_t(void*, void*, void*);
typedef int CNAPI mluCvtRgbx2RgbxDestroy_t(void*);

typedef int CNAPI mluCvtRgbx2YuvInit_t(void**, int, int, const char*, const char*, const char*);
typedef int CNAPI mluCvtRgbx2YuvExec_t(void*, void*, void*, void*);
typedef int CNAPI mluCvtRgbx2YuvDestroy_t(void*);

typedef int CNAPI mluCvtYuv2RgbxInit_t(void**, int, int, const char*, const char*, const char*);
typedef int CNAPI mluCvtYuv2RgbxExec_t(void*, void*, void*, void*);
typedef int CNAPI mluCvtYuv2RgbxDestroy_t(void*);

typedef int CNAPI mluScaleCvtYuv2RgbxInit_t(void**, int, int, int, int, const char*, const char*, const char*);
typedef int CNAPI mluScaleCvtYuv2RgbxExec_t(void*, void*, void*, void*);
typedef int CNAPI mluScaleCvtYuv2RgbxDestroy_t(void*);

typedef int CNAPI mluScaleRgbxInit_t(void**, int, int, int, int, const char*, const char*);
typedef int CNAPI mluScaleRgbxExec_t(void*, void*, void*);
typedef int CNAPI mluScaleRgbxExecPad_t(void*, void*, void*);
typedef int CNAPI mluScaleRgbxExecCrop_t(void*, void*, void*, int, int, int, int, int, int, int, int);
typedef int CNAPI mluScaleRgbxDestroy_t(void*);

typedef int CNAPI mluScaleYuvInit_t(void**, int, int, int, int, const char*, const char*);
typedef int CNAPI mluScaleYuvExec_t(void*, void*, void*, void*, void*);
typedef int CNAPI mluScaleYuvExecPad_t(void*, void*, void*, void*, void*);
typedef int CNAPI mluScaleYuvExecCrop_t(void*, void*, void*, void*, void*, int, int, int, int, int, int, int, int);
typedef int CNAPI mluScaleYuvDestroy_t(void*);

typedef int CNAPI mluOpGetVersion_t(void);


typedef struct mluOpFuncList {
  mluCvtRgbx2RgbxInit_t        *mluCvtRgbx2RgbxInit;
  mluCvtRgbx2RgbxExec_t        *mluCvtRgbx2RgbxExec;
  mluCvtRgbx2RgbxDestroy_t     *mluCvtRgbx2RgbxDestroy;
  mluCvtRgbx2YuvInit_t         *mluCvtRgbx2YuvInit;
  mluCvtRgbx2YuvExec_t         *mluCvtRgbx2YuvExec;
  mluCvtRgbx2YuvDestroy_t      *mluCvtRgbx2YuvDestroy;
  mluCvtYuv2RgbxInit_t         *mluCvtYuv2RgbxInit;
  mluCvtYuv2RgbxExec_t         *mluCvtYuv2RgbxExec;
  mluCvtYuv2RgbxDestroy_t      *mluCvtYuv2RgbxDestroy;
  mluScaleCvtYuv2RgbxInit_t    *mluScaleCvtYuv2RgbxInit;
  mluScaleCvtYuv2RgbxExec_t    *mluScaleCvtYuv2RgbxExec;
  mluScaleCvtYuv2RgbxDestroy_t *mluScaleCvtYuv2RgbxDestroy;
  mluScaleRgbxInit_t           *mluScaleRgbxInit;
  mluScaleRgbxExec_t           *mluScaleRgbxExec;
  mluScaleRgbxExecPad_t        *mluScaleRgbxExecPad;
  mluScaleRgbxExecCrop_t       *mluScaleRgbxExecCrop;
  mluScaleRgbxDestroy_t        *mluScaleRgbxDestroy;
  mluScaleYuvInit_t            *mluScaleYuvInit;
  mluScaleYuvExec_t            *mluScaleYuvExec;
  mluScaleYuvExecPad_t         *mluScaleYuvExecPad;
  mluScaleYuvExecCrop_t        *mluScaleYuvExecCrop;
  mluScaleYuvDestroy_t         *mluScaleYuvDestroy;
  mluOpGetVersion_t            *mluOpGetVersion;
} mluOpFuncList;

#define MLUOP_HANDLE void *
#define MLUOP_LIBNAME "libeasyOP.so"

#if !defined(MLU_LOAD_FUNC) || !defined(MLU_SYM_FUNC)
#  include <dlfcn.h>
#  define MLU_LOAD_FUNC(path)    dlopen((path), RTLD_LAZY)
#  define MLU_SYM_FUNC(lib, sym) dlsym((lib), (sym))
#  define MLU_FREE_FUNC(lib)     dlclose(lib)
# endif

#if !defined(MLU_ERROR_LOG) || !defined(MLU_INFO_LOG)
# include <stdio.h>
# define MLU_INFO_LOG(ctx, msg, ...)  fprintf(stdout, (msg), ##__VA_ARGS__)
# define MLU_ERROR_LOG(ctx, msg, ...) fprintf(stderr, (msg), ##__VA_ARGS__)
#endif

#define MLU_LOAD_LIBRARY(lib, path)                          \
  do {                                                       \
    if (!(lib = MLU_LOAD_FUNC(path))) {                      \
      MLU_ERROR_LOG("Cannot load %s\n", path);               \
      goto error;                                            \
    }                                                        \
  } while (0)

#define MLU_UNLOAD_LIBRARY(lib)                              \
  do {                                                       \
    MLU_FREE_FUNC(lib);                                      \
  } while (0)

#define MLU_LOAD_SYMBOL(fun, tp, symbol, lib)                \
  do {                                                       \
    if (!(fun = (tp*)MLU_SYM_FUNC(lib, symbol))) {           \
      MLU_ERROR_LOG(NULL, "Cannot load %s\n", symbol);       \
      goto error;                                            \
    }                                                        \
  } while (0)

class mluOpAPI {
public:
  mluOpAPI() {
    int ret = 0;
    ret = loadMluOpLIbrary();
    if (ret) {
      std::cout << "load mluop library failed" << std::endl;
      exit(1);
    }
    ret = loadMluOpFuncs();
    if (ret) {
      std::cout << "load mluop funcs failed" << std::endl;
      exit(1);
    }
  }
  ~mluOpAPI() {
    if (m_api_hdl) unloadMLuOpLibrary();
    m_api.release();
  }
  std::unique_ptr<mluOpFuncList> getAPI() {
    std::lock_guard<std::mutex> lock(m_dl_lock);
    return std::move(m_api);
  }

protected:
    MLUOP_HANDLE m_api_hdl;
    std::mutex m_dl_lock;

private:
    std::unique_ptr<mluOpFuncList> m_api;

private:
  int loadMluOpLIbrary() {
    MLU_LOAD_LIBRARY(m_api_hdl, MLUOP_LIBNAME);
    return 0;
  error:
    m_api_hdl = NULL;
    return -1;
  }
  int loadMluOpFuncs() {
    m_api.reset(new mluOpFuncList);
    MLU_LOAD_SYMBOL(m_api->mluCvtRgbx2RgbxInit, mluCvtRgbx2RgbxInit_t, "mluOpConvertRgbx2RgbxInit", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluCvtRgbx2RgbxExec, mluCvtRgbx2RgbxExec_t, "mluOpConvertRgbx2RgbxExec", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluCvtRgbx2RgbxDestroy, mluCvtRgbx2RgbxDestroy_t, "mluOpConvertRgbx2RgbxDestroy", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluCvtRgbx2YuvInit, mluCvtRgbx2YuvInit_t, "mluOpConvertRgbx2YuvInit", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluCvtRgbx2YuvExec, mluCvtRgbx2YuvExec_t, "mluOpConvertRgbx2YuvExec", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluCvtRgbx2YuvDestroy, mluCvtRgbx2YuvDestroy_t, "mluOpConvertRgbx2YuvDestroy", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluCvtYuv2RgbxInit, mluCvtYuv2RgbxInit_t, "mluOpConvertYuv2RgbxInit", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluCvtYuv2RgbxExec, mluCvtYuv2RgbxExec_t, "mluOpConvertYuv2RgbxExec", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluCvtYuv2RgbxDestroy, mluCvtYuv2RgbxDestroy_t, "mluOpConvertYuv2RgbxDestroy", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleCvtYuv2RgbxInit, mluScaleCvtYuv2RgbxInit_t, "mluOpResizeCvtInit", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleCvtYuv2RgbxExec, mluScaleCvtYuv2RgbxExec_t, "mluOpResizeCvtExec", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleCvtYuv2RgbxDestroy, mluScaleCvtYuv2RgbxDestroy_t, "mluOpResizeCvtDestroy", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleRgbxInit, mluScaleRgbxInit_t, "mluOpResizeRgbxInit", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleRgbxExec, mluScaleRgbxExec_t, "mluRpResizeRgbxExec", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleRgbxExecPad, mluScaleRgbxExecPad_t, "mluOpResizeRgbxExecPad", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleRgbxExecCrop, mluScaleRgbxExecCrop_t, "mluOpResizeRgbxExecRoi", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleRgbxDestroy, mluScaleRgbxDestroy_t, "mluOpResizeRgbxDestroy", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleYuvInit, mluScaleYuvInit_t, "mluOpResizeYuvInit", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleYuvExec, mluScaleYuvExec_t, "mluOpResizeYuvExec", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleYuvExecPad, mluScaleYuvExecPad_t, "mluOpResizeYuvExecPad", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleYuvExecCrop, mluScaleYuvExecCrop_t, "mluOpResizeYuvExecRoi", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluScaleYuvDestroy, mluScaleYuvDestroy_t, "mluOpResizeYuvDestroy", m_api_hdl);
    MLU_LOAD_SYMBOL(m_api->mluOpGetVersion, mluOpGetVersion_t, "mluOpGetVersion", m_api_hdl);
    return 0;
  error:
    if (m_api_hdl) {
      MLU_UNLOAD_LIBRARY(m_api_hdl);
      m_api_hdl = NULL;
    }
    return -1;
  }
  void unloadMLuOpLibrary() {
    MLU_UNLOAD_LIBRARY(m_api_hdl);
  }
};

#undef MLUOP_HANDLE
#undef MLUOP_LIBNAME
#undef MLU_LOAD_FUNC
#undef MLU_SYM_FUNC
#undef MLU_FREE_FUNC
#undef MLU_LOAD_LIBRARY
#undef MLU_UNLOAD_LIBRARY
#undef MLU_LOAD_SYMBOL

#define ALIGN_Y_SCALE 1
#define ALIGN_R_SCALE 1
#define ALIGN_Y2R_CVT 1
#define ALIGN_R2Y_CVT 1
#define ALIGN_RESIZE_CVT 1
#define PAD_UP(x, y) ((x / y + (int)((x) % y > 0)) * y)

typedef enum {
  CN_DEPTH_NONE = -1,
  CN_DEPTH_8U  = 0,
  CN_DEPTH_8S  = 1,
  CN_DEPTH_16U = 2,
  CN_DEPTH_16S = 3,
  CN_DEPTH_32U = 4,
  CN_DEPTH_32S = 5,
  CN_DEPTH_16F = 6,
  CN_DEPTH_32F = 7,
} cnDepth_t;

typedef enum {
  CN_COLOR_SPACE_BT_601 = 0,
  CN_COLOR_SPACE_BT_709 = 1,
} cnColorSpace;

#endif /* __MLUOP_CONTEXT_H__ */
