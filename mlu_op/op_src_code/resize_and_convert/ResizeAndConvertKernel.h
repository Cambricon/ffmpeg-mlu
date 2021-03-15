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

#include "half.hpp"
extern "C" {

void ResizeAndConvertKernelMlu270(half* dst, half* srcY, half* srcUV, half* yuvFilter, half* yuvBias, int s_row, int s_col,
                            int d_row, int d_col, int roi_x, int roi_y, int roi_w, int roi_h, int channelIn,
                            int channelOut, int layerIn, int layerOut, int input2half, int output2uint, int scaleXInt,
                            int scaleYInt, int batchNum, int pad, uint32_t* cycleNum);

void ResizeAndConvertKernelMlu220(half* dst, half* srcY, half* srcUV, half* yuvFilter, half* yuvBias, int s_row, int s_col,
                            int d_row, int d_col, int roi_x, int roi_y, int roi_w, int roi_h, int channelIn,
                            int channelOut, int layerIn, int layerOut, int input2half, int output2uint, int scaleXInt,
                            int scaleYInt, int batchNum, int pad, uint32_t* cycleNum);

void ResizeAndConvertKernel(half* dst, half* srcY, half* srcUV, half* yuvFilter, half* yuvBias, int s_row, int s_col,
                            int d_row, int d_col, int roi_x, int roi_y, int roi_w, int roi_h, int channelIn,
                            int channelOut, int layerIn, int layerOut, int input2half, int output2uint, int scaleXInt,
                            int scaleYInt, int batchNum, int pad);

}
