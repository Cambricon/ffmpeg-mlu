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

#include "mluop.h"
#include "mluop_list.h"
#include "test_mluop.h"
#include <iostream>

uint32_t getPixFmtChannelNum(cncvPixelFormat pixfmt) {
  if (pixfmt == CNCV_PIX_FMT_BGR || pixfmt == CNCV_PIX_FMT_RGB) {
    return 3;
  } else if (pixfmt == CNCV_PIX_FMT_ABGR || pixfmt == CNCV_PIX_FMT_ARGB ||
             pixfmt == CNCV_PIX_FMT_BGRA || pixfmt == CNCV_PIX_FMT_RGBA) {
    return 4;
  } else if (pixfmt == CNCV_PIX_FMT_NV12 || pixfmt == CNCV_PIX_FMT_NV21) {
    return 1;
  } else {
    std::cout << "don't suport pixfmt" << std::endl;
    return 0;
  }
}

uint32_t getSizeOfDepth(cncvDepth_t depth) {
  if (depth == CNCV_DEPTH_8U) {
    return 1;
  } else if (depth == CNCV_DEPTH_16F) {
    return 2;
  } else if (depth == CNCV_DEPTH_32F) {
    return 4;
  }
  return 1;
}

cncvPixelFormat getCNCVPixFmtFromPixindex(const char* pix_fmt) {
  if (strncmp(pix_fmt, "NV12", 4) == 0 ||
      strncmp(pix_fmt, "nv12", 4) == 0) {
        return CNCV_PIX_FMT_NV12;
  } else if (strncmp(pix_fmt, "NV21", 4) == 0 ||
             strncmp(pix_fmt, "nv21", 4) == 0) {
      return CNCV_PIX_FMT_NV21;
  } else if (strcmp(pix_fmt , "RGB24") == 0 ||
             strcmp(pix_fmt , "rgb24") == 0) {
        return  CNCV_PIX_FMT_RGB;
  } else if (strcmp(pix_fmt , "BGR24") == 0 ||
             strcmp(pix_fmt , "bgr24") == 0) {
      return CNCV_PIX_FMT_BGR;
  } else if (strncmp(pix_fmt, "ARGB", 4) == 0 ||
             strncmp(pix_fmt, "argb", 4) == 0) {
      return CNCV_PIX_FMT_ARGB;
  } else if (strncmp(pix_fmt, "ABGR", 4) == 0 ||
             strncmp(pix_fmt, "abgr", 4) == 0) {
      return CNCV_PIX_FMT_ABGR;
  } else if (strncmp(pix_fmt, "RGBA", 4) == 0 ||
             strncmp(pix_fmt, "rgba", 4) == 0) {
      return CNCV_PIX_FMT_RGBA;
  } else if (strncmp(pix_fmt, "BGRA", 4) == 0 ||
             strncmp(pix_fmt, "bgra", 4) == 0) {
      return CNCV_PIX_FMT_BGRA;
  } else {
      printf("unsupported pixel format\n");
      return CNCV_PIX_FMT_INVALID;
    }
}

int readRawYUV(const char *filename, uint32_t width, uint32_t height,
               uint8_t **YUV) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    perror("Error opening yuv image for read");
    return 1;
  }

  // check file size
  fseek(fp, 0, SEEK_END);
  uint32_t size = ftell(fp);
  if (size != (width * height + 2 * ((width + 1) / 2) * ((height + 1) / 2))) {
    fprintf(stderr, "Wrong size of yuv image : %d bytes, expected %d bytes\n",
            size,
            (width * height + 2 * ((width + 1) / 2) * ((height + 1) / 2)));
    fclose(fp);
    return 2;
  }
  fseek(fp, 0, SEEK_SET);

  *YUV = (uint8_t*)malloc(size);
  size_t result = fread(*YUV, 1, size, fp);
  if (result != size) {
    perror("Error reading yuv image");
    fclose(fp);
    return 3;
  }
  fclose(fp);
  return 0;
}

// write a raw yuv image file
// ffmpeg -s cif -pix_fmt nv12 -i test1.yuv test_cif.jpg
int saveRawYUV(const char *filename, uint32_t width, uint32_t height,
               const uint8_t *YUV, size_t y_stride, size_t uv_stride) {
  FILE *fp = fopen(filename, "wb");
  if (!fp) {
    perror("Error opening yuv image for write");
    return 1;
  }

  if (y_stride == width) {
    fwrite(YUV, 1, width * height, fp);
    YUV += width * height;
  } else {
    for (uint32_t y = 0; y < height; ++y) {
      fwrite(YUV, 1, width, fp);
      YUV += y_stride;
    }
  }

  if (uv_stride == ((width + 1) / 2)) {
    fwrite(YUV, 1, ((width + 1) / 2) * ((height + 1) / 2) * 2, fp);
  } else {
    for (uint32_t y = 0; y < ((height + 1) / 2); ++y) {
      fwrite(YUV, 1, ((width + 1) / 2), fp);
      YUV += uv_stride;
    }

    for (uint32_t y = 0; y < ((height + 1) / 2); ++y) {
      fwrite(YUV, 1, ((width + 1) / 2), fp);
      YUV += uv_stride;
    }
  }

  fclose(fp);
  return 0;
}

int set_cnrt_ctx(unsigned int device_id, cnrtChannelType_t channel_id) {
  cnrtDev_t dev;
  cnrtRet_t ret;
  ret = cnrtGetDeviceHandle(&dev, device_id);
  CNRT_ERROR_CHECK(ret);
  ret = cnrtSetCurrentDevice(dev);
  CNRT_ERROR_CHECK(ret);
  if (channel_id >= CNRT_CHANNEL_TYPE_0) {
    ret = cnrtSetCurrentChannel(channel_id);
    CNRT_ERROR_CHECK(ret);
  }
  return 0;
}
