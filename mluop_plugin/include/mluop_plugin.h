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
#ifndef __MLUOP_PLUGIN_H__
#define __MLUOP_PLUGIN_H__

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

typedef void *HANDLE;

int mluop_resize_yuv_init(HANDLE *h, int input_w, int input_h,
                          int input_stride_in_bytes, int output_w, int output_h,
                          int output_stride_in_bytes, int device_id);
int mluop_resize_yuv_exec(HANDLE h, void *input_y, void *input_uv,
                          void *output_y, void *output_uv);
int mluop_resize_yuv_destroy(HANDLE h);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* __MLUOP_PLUGIN_H__ */