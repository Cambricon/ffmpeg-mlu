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
#ifndef __MLUOP_H__
#define __MLUOP_H__

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "cncv.h"
#include "cnrt.h"

#define MLUOP_MAJOR 2
#define MLUOP_MINOR 4
#define MLUOP_PATCH 0
#define MLUOP_VERSION (MLUOP_MAJOR * 1000 + MLUOP_MINOR * 100 + MLUOP_PATCH)

#define ALIGN_Y_SCALE 1
#define ALIGN_R_SCALE 1
#define ALIGN_Y2R_CVT 1
#define ALIGN_R2Y_CVT 1
#define ALIGN_RESIZE_CVT 1
#define PAD_UP(x, y) ((x / y + (int)((x) % y > 0)) * y)

#define MLUOP_RT_CHECK(ret, msg)                                               \
  if (ret != CNRT_RET_SUCCESS) {                                               \
    fprintf(stderr, "Error: %s, ret:%d, func:%s, line:%d\n"                    \
            msg, ret, __func__, __LINE__);                                     \
    return -1;                                                                 \
  }
#define MLUOP_CV_CHECK(ret, msg)                                               \
  if (ret != CNCV_STATUS_SUCCESS) {                                            \
    fprintf(stderr, "Error: %s, ret:%d, func:%s, line:%d\n"                    \
            msg, ret, __func__, __LINE__);                                     \
    return -1;                                                                 \
  }

typedef void *HANDLE;
typedef enum {
  CN_DEPTH_NONE = -1,
  CN_DEPTH_8U = 0,
  CN_DEPTH_8S = 1,
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

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

// deprecated
int mluop_resize_yuv_init(HANDLE*, int, int, int, int, const char*, const char*);
int mluop_resize_yuv_exec(HANDLE, void*, void*, void*, void*);
int mluop_resize_pad_yuv_exec(HANDLE, void*, void*, void*, void*);
int mluop_resize_roi_yuv_exec(HANDLE, void*, void*, void*, void*,
                              uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t, uint32_t);
int mluop_resize_yuv_destroy(HANDLE);
// recommende
int mluOpResizeYuvInit(HANDLE*, int, int, int, int, const char*, const char*);
int mluOpResizeYuvExec(HANDLE, void*, void*, void*, void*);
int mluOpResizeYuvExecPad(HANDLE, void*, void*, void*, void*);
int mluOpResizeYuvExecRoi(HANDLE, void*, void*, void*, void*,
                          uint32_t, uint32_t, uint32_t, uint32_t,
                          uint32_t, uint32_t, uint32_t, uint32_t);
int mluOpResizeYuvDestroy(HANDLE);
// deprecated
int mluop_resize_rgbx_init(HANDLE*, int, int, int, int, const char*, const char*);
int mluop_resize_rgbx_exec(HANDLE, void*, void*);
int mluop_resize_pad_rgbx_exec(HANDLE, void*, void*);
int mluop_resize_roi_rgbx_exec(HANDLE, void*, void*,
                              uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t, uint32_t);
int mluop_resize_rgbx_destroy(HANDLE);
// recommende
int mluOpResizeRgbxInit(HANDLE*, int, int, int, int, const char*, const char*);
int mluRpResizeRgbxExec(HANDLE, void*, void*);
int mluOpResizeRgbxExecPad(HANDLE, void*, void*);
int mluOpResizeRgbxExecRoi(HANDLE, void*, void*,
                              uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t, uint32_t);
int mluOpResizeRgbxDestroy(HANDLE);
// deprecated
int mluop_convert_yuv2rgbx_init(HANDLE*, int, int,
                                const char*, const char*, const char*);
int mluop_convert_yuv2rgbx_exec(HANDLE, void*, void*, void*);
int mluop_convert_yuv2rgbx_destroy(HANDLE);
// recommende
int mluOpConvertYuv2RgbxInit(HANDLE*, int, int,
                                const char*, const char*, const char*);
int mluOpConvertYuv2RgbxExec(HANDLE, void*, void*, void*);
int mluOpConvertYuv2RgbxDestroy(HANDLE);
// deprecated
int mluop_convert_rgbx2yuv_init(HANDLE*, int, int,
                                const char*, const char*, const char*);
int mluop_convert_rgbx2yuv_exec(HANDLE, void*, void*, void*);
int mluop_convert_rgbx2yuv_exec_roi(HANDLE, void*, void*, void*,
                                    int, int, int, int);
int mluop_convert_rgbx2yuv_destroy(HANDLE);
// recommende
int mluOpConvertRgbx2YuvInit(HANDLE*, int, int,
                                const char*, const char*, const char*);
int mluOpConvertRgbx2YuvExec(HANDLE, void*, void*, void*);
int mluOpConvertRgbx2YuvExecRoi(HANDLE, void*, void*, void*,
                                    int, int, int, int);
int mluOpConvertRgbx2YuvDestroy(HANDLE);
// deprecated
int MluopConvertRgbx2RgbxInit(HANDLE*, int, int,
                              const char*, const char*, const char*);
int MluopConvertRgbx2RgbxExec(HANDLE, void*, void*);
int MluopConvertRgbx2RgbxDestroy(HANDLE);
// recommende
int mluOpConvertRgbx2RgbxInit(HANDLE*, int, int,
                              const char*, const char*, const char*);
int mluOpConvertRgbx2RgbxExec(HANDLE, void*, void*);
int mluOpConvertRgbx2RgbxDestroy(HANDLE);
// deprecated
int MluopResizeCvtInit(HANDLE*, int, int, int, int, const char*, const char*,
                       const char*);
int MluopResizeCvtExec(HANDLE, void*, void*, void*);
int MluopResizeCvtPadExec(HANDLE, void*, void*, void*);
int MluopResizeCvtDestroy(HANDLE);
// recommende
int mluOpResizeCvtInit(HANDLE*, int, int, int, int, const char*, const char*,
                       const char*);
int mluOpResizeCvtExec(HANDLE, void*, void*, void*);
int mluOpResizeCvtExecPad(HANDLE, void*, void*, void*);
int mluOpResizeCvtDestroy(HANDLE);

int mluOpGetVersion (void);

static uint32_t getSizeOfDepth(cncvDepth_t depth) {
  if (depth == CNCV_DEPTH_8U) {
    return 1;
  } else if (depth == CNCV_DEPTH_16F) {
    return 2;
  } else if (depth == CNCV_DEPTH_32F) {
    return 4;
  }
  return 1;
}

static uint32_t getPixFmtChannelNum(cncvPixelFormat pixfmt) {
  if (pixfmt == CNCV_PIX_FMT_BGR || pixfmt == CNCV_PIX_FMT_RGB) {
    return 3;
  } else if (pixfmt == CNCV_PIX_FMT_ABGR || pixfmt == CNCV_PIX_FMT_ARGB ||
             pixfmt == CNCV_PIX_FMT_BGRA || pixfmt == CNCV_PIX_FMT_RGBA) {
    return 4;
  } else if (pixfmt == CNCV_PIX_FMT_NV12 || pixfmt == CNCV_PIX_FMT_NV21) {
    return 1;
  } else {
    printf("Unsupported pixfmt(%d)\n", pixfmt);
    return 0;
  }
}

static cncvDepth_t getCNCVDepthFromIndex(const char* depth) {
  if (strcmp(depth, "8U") == 0 || strcmp(depth, "8u") == 0) {
    return CNCV_DEPTH_8U;
  } else if (strcmp(depth, "16F") == 0 || strcmp(depth, "16f") == 0) {
    return CNCV_DEPTH_16F;
  } else if (strcmp(depth, "32F") == 0 || strcmp(depth, "32f") == 0) {
    return CNCV_DEPTH_32F;
  } else {
    printf("Unsupported depth(%s)\n", depth);
    return CNCV_DEPTH_INVALID;
  }
}

static cncvPixelFormat getCNCVPixFmtFromPixindex(const char* pix_fmt) {
  if (strcmp(pix_fmt, "NV12") == 0 || strcmp(pix_fmt, "nv12") == 0) {
    return CNCV_PIX_FMT_NV12;
  } else if(strcmp(pix_fmt, "NV21") == 0 || strcmp(pix_fmt, "nv21") == 0) {
    return CNCV_PIX_FMT_NV21;
  } else if(strcmp(pix_fmt, "RGB24") == 0 || strcmp(pix_fmt, "rgb24") == 0) {
    return CNCV_PIX_FMT_RGB;
  } else if(strcmp(pix_fmt, "BGR24") == 0 || strcmp(pix_fmt, "bgr24") == 0) {
    return CNCV_PIX_FMT_BGR;
  } else if(strcmp(pix_fmt, "ARGB") == 0 || strcmp(pix_fmt, "argb") == 0) {
    return CNCV_PIX_FMT_ARGB;
  } else if(strcmp(pix_fmt, "ABGR") == 0 || strcmp(pix_fmt, "abgr") == 0) {
    return CNCV_PIX_FMT_ABGR;
  } else if(strcmp(pix_fmt, "RGBA") == 0 || strcmp(pix_fmt, "rgba") == 0) {
    return CNCV_PIX_FMT_RGBA;
  } else if (strcmp(pix_fmt, "BGRA") == 0 || strcmp(pix_fmt, "bgra") == 0) {
    return CNCV_PIX_FMT_BGRA;
  } else {
    printf("Unsupported pixfmt(%s)\n", pix_fmt);
    return CNCV_PIX_FMT_INVALID;
  }
}

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* __MLUOP_H__ */
