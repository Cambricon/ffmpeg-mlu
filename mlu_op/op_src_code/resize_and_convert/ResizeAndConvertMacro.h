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

#ifndef _SAMPLES_RESIZEDCONVERT_MACRO_H_
#define _SAMPLES_RESIZEDCONVERT_MACRO_H_

// COLORMODE
#define RGBA_TO_RGBA 0
#define YUV_TO_RGBA_NV12 1
#define YUV_TO_RGBA_NV21 2
#define YUV_TO_BGRA_NV12 3
#define YUV_TO_BGRA_NV21 4
#define YUV_TO_ARGB_NV12 5
#define YUV_TO_ARGB_NV21 6
#define YUV_TO_ABGR_NV12 7
#define YUV_TO_ABGR_NV21 8

// DATAMODE
#define FP16_TO_FP16 0
#define FP16_TO_UINT8 1
#define UINT8_TO_FP16 2
#define UINT8_TO_UINT8 3

// IOTYPE
#define RGBA 0
#define BGRA 1
#define ARGB 2
#define ABGR 3
#define RGB 4
#define BGR 5
#define GRAY 6
#define YUVNV12 7
#define YUVNV21 8

// DATATYPE
#define UINT8 0
#define FP16 1

#define PAD_UP(x, y) (x / y + (int)((x) % y > 0)) * y
#define PAD_DN(x, y) (x / y) * y
#if (__BANG_ARCH__ >= 200) || CNSTK_MLU270
#define PAD_SIZE 64
#define CI 64
#define MULTCI 64
#define CO 256
#define LT_NUM 64
#else
#define PAD_SIZE 32
#define CI 32
#define MULTCI 64
#define CO 128
#define LT_NUM 16
#endif

#define IN_DATA_TYPE unsigned char
#define OUT_DATA_TYPE unsigned char
typedef struct ioParams {
  int color;
  int data;
} ioParams;

// #define ENABLE_MACRO
#endif  // _SAMPLES_RESIZEDCONVERT_MACRO_H_
