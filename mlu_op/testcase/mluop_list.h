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
#ifndef __MLUOP_LIST_H__
#define __MLUOP_LIST_H__

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/*------------- yuv2yuv resize cncv--------------*/
void yuv2yuv_resize_op(void *ctx_, char **argv);
void *process_resize_yuv(void *ctx_);

/*------------- rgbx2rgbx resize cncv--------------*/
void rgbx2rgbx_resize_op(void *ctx_, char **argv);
void *process_resize_rgbx(void *ctx_);

/*------------- yuv2rgbx convert cncv--------------*/
void yuv2rgbx_convert_op(void *ctx_, char **argv);
void *process_convert_yuv2rgbx(void *ctx_);

/*------------- rgbx2yuv convert cncv--------------*/
void rgbx2yuv_convert_op(void *ctx_, char **argv);
void *process_convert_rgbx2yuv(void *ctx_);

/*------------- rgbx2rgbx convert cncv--------------*/
void Rgbx2RgbxConvertOp(void *ctx_, char **argv);
void *ProcessConvertRgbx2Rgbx(void *ctx_);

/*------------- yuv2rgbx resize convert cncv--------------*/
void Yuv2RgbxResizeCvtOp(void *ctx_, char **argv);
void *ProcessResizeCvtYuv2Rgbx(void *ctx_);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* __MLUOP_LIST_H__ */
