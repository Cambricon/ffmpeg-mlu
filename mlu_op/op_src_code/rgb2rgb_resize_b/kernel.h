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
#ifndef KERNEL_H_
#define KERNEL_H_

// Host cc need ignore these function attributes of BANG language
#ifndef __BANG__
#define __mlu_global__
#define __mlu_device__
#define __mlu_func__
#endif

/******************************************************************************
 * Macros for BANG
 ******************************************************************************/
#if __BANG_ARCH__ == 270
#define MAX_WRAM_SIZE (1024 * 1024)
#define WRAM_LT_STRIDE (1024 * 1024 / 64)
#elif(__BANG_ARCH__ == 290 || __BANG_ARCH__ == 220)
#define MAX_WRAM_SIZE (512 * 1024)
#define WRAM_LT_STRIDE (512 * 1024 / 64)
#else  // default size
#define MAX_WRAM_SIZE (1024 * 1024)
#define WRAM_LT_STRIDE (1024 * 1024 / 64)
#endif

#define DDR_ALIGN_MAP3 (1024 * 16)  // 16KB
#define NFU_ALIGN_SIZE 128          // Byte
#define WRAM_ALIGN_SIZE 64
#define LT_NUM 64

#define CORE_DIM 4
#define CLUSTER_DIM_OF_BLOCK 0
#define CLUSTER_DIM_OF_UNION1 1
#define CLUSTER_DIM_OF_UNION2 2
#define CLUSTER_DIM_OF_UNION4 4
#define CLUSTER_DIM_OF_UNION8 8
#define CLUSTER_DIM_OF_UNION16 16

#define MAX_NRAM_SIZE (1024 * NFU_ALIGN_SIZE * 3)    // 384KB, 128KB reserved for cncc  /* NOLINT */
#define MAX_SRAM_SIZE (MAX_NRAM_SIZE * CORE_DIM)     // 1536KB, 512KB reserved for cncc  /* NOLINT */
#define THRESHOLD_SIZE_OF_BLOCK (NFU_ALIGN_SIZE)     // Block task has no Shared-RAM  /* NOLINT */
#define THRESHOLD_SIZE_OF_UNION (MAX_NRAM_SIZE / 6)  // Split NRAM to 6 * 64KB  /* NOLINT */

#ifndef PAD_UP
#define PAD_UP(x, y) ((x / y + (int)((x) % y > 0)) * y)  // NOLINT
#endif

#ifndef PAD_DOWN
#define PAD_DOWN(x, y) ((x / y) * y)
#endif

#define CEIL_ALIGN(x, align) ((x) + (align)-1) / (align) * (align)
#define FLOOR_ALIGN(x, align) (x) / (align) * (align)

#endif  // KERNELS_KERNEL_H_
