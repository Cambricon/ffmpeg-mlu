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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUKType WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUKType NOKType LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENKType SHALL THE AUTHORS OR COPYRIGHKType HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORKType OR OTHERWISE, ARISING FROM, OUKType OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *************************************************************************/
#ifndef RESIZE_RGB_MLU_H_
#define RESIZE_RGB_MLU_H_
 void MLUBlockKernelResizeRgbx(
                              void **src_gdram,
                              void **dst_gdram,
                              uint32_t depth,
                              uint32_t *src_rois,
                              uint32_t batch_size,
                              uint32_t s_height,
                              uint32_t s_width,
                              uint32_t s_stride,
                              uint32_t d_x,
                              uint32_t d_y,
                              uint32_t d_col,
                              uint32_t d_row,
                              uint32_t d_stride,
                              uint32_t chn_num);

#endif  // RESIZE_RGB_MLU_H_
