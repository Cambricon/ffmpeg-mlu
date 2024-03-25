/*************************************************************************
 * Copyright (C) [2024] by Cambricon, Inc.
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
#include <sys/time.h>
#include <chrono>
#include <ctime>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <string>
#include <fstream>

#include "cnrt.h"
#include "mluop.h"

extern cncvStatus_t cncvAlphaBlend_BasicROI(cncvHandle_t handle,
        const cncvImageDescriptor src1_desc,
        const cncvRect src1_roi,
        void *src1,
        float alpha,
        const cncvImageDescriptor src2_desc,
        void *src2,
        float beta,
        float gamma,
        const cncvImageDescriptor dst_desc,
        void *dst);

extern cncvStatus_t cncvYuvToRgbx_V2(cncvHandle_t handle,
        const uint32_t batch_size,
        const cncvImageDescriptor src_desc,
        cncvBufferList_t src,
        const cncvImageDescriptor dst_desc,
        cncvBufferList_t dst);

extern cncvStatus_t cncvRgbxToYuv_BasicROIP2(cncvHandle_t handle,
        const cncvImageDescriptor src_desc,
        const cncvRect src_roi,
        const void *src,
        const cncvImageDescriptor dst_desc,
        void *y,
        void *uv);

// resize and convert
extern cncvStatus_t cncvResizeConvertCreate(cncvHandle_t handle,
        cncvInterpolation interp,
        cncvResizeConvertOp_t *op);

extern cncvStatus_t cncvResizeConvertDestroy(cncvResizeConvertOp_t op);

extern cncvStatus_t cncvResizeConvertGetAuxDataSize(cncvResizeConvertOp_t op,
        size_t *aux_data_size);

extern cncvStatus_t cncvResizeConvertInitAuxData(cncvResizeConvertOp_t op,
        void *aux_data_host);

extern cncvStatus_t cncvResizeConvertSetOp_AdvancedROI(cncvResizeConvertOp_t op,
        const uint32_t batch_size,
        const cncvImageDescriptor *psrc_descs,
        const cncvRect *src_rois,
        const cncvImageDescriptor *pdst_descs,
        const cncvRect *dst_rois);

extern cncvStatus_t cncvResizeConvertApply_AdvancedROI(cncvResizeConvertOp_t op,
        cncvBufferList_t src,
        cncvBufferList_t dst,
        void *aux_data_device);

struct CvOverlayPrivate {
public:
    uint32_t overlay_src_bg_width;
    uint32_t overlay_src_bg_height;
    uint32_t src_bg_pixfmt_plane_num;
    uint32_t src_fg_pixfmt_plane_num;
    uint32_t dst_pixfmt_plane_num;
    cncvColorSpace src_bg_colorspace;
    cncvColorSpace src_fg_colorspace;

    // cncvYuvToRgbx
    bool bg_yuv2rgb_enable;
    void **op_yuv2rgb_src_bg_ptrs_cpu;
    void **op_yuv2rgb_src_bg_ptrs_mlu;
    void **op_yuv2rgb_dst_bg_ptrs_cpu;
    void **op_yuv2rgb_dst_bg_ptrs_mlu;
    void *op_yuv2rgb_dst_blend_src_bg_ptr_mlu; // malloc inner
    cncvImageDescriptor op_yuv2rgb_dst_bg_desc;
    cncvImageDescriptor op_yuv2rgb_src_bg_desc;
    uint32_t op_yuv2rgb_dst_bg_size;
    uint32_t yuv2rgb_src_pixfmt_plane_num;
    uint32_t yuv2rgb_dst_pixfmt_plane_num;

    // cncvResizeConvertApply_AdvancedROI
    bool fg_resize_convert_enable;
    uint32_t op_resize_convert_input_w;
    uint32_t op_resize_convert_input_h;
    uint32_t op_resize_convert_output_w;
    uint32_t op_resize_convert_output_h;
    uint32_t resize_convert_src_pixfmt_plane_num;
    uint32_t resize_convert_dst_pixfmt_plane_num;
    void *op_resize_convert_dst_blend_src_fg_ptr_mlu; // malloc inner
    void *op_resize_convert_workspace_mlu;
    void *op_resize_convert_workspace_cpu;
    void **op_resize_convert_src_fg_ptrs_cpu_;
    void **op_resize_convert_src_fg_ptrs_mlu_;
    void **op_resize_convert_dst_fg_ptrs_cpu_;
    void **op_resize_convert_dst_fg_ptrs_mlu_;
    size_t probe_resize_convert_workspace_size;

    cncvImageDescriptor op_resize_convert_src_desc;
    cncvImageDescriptor op_resize_convert_dst_desc;
    cncvRect op_resize_convert_src_rois;
    cncvRect op_resize_convert_dst_rois;
    cncvResizeConvertOp_t rc_op;
    uint32_t last_roi_w, last_roi_h;
    std::string op_resize_convert_src_fg_pixfmt;
    std::string op_resize_convert_dst_fg_pixfmt;

    // cncvAlphaBlend_BasicROI
    // overlay out pixfmt is yuv420: malloc inner, rgbx: malloc app
    void *op_alphablend_dst_ptr_mlu;
    cncvPixelFormat dst_pix_fmt;
    cncvImageDescriptor op_alphablend_src_bg_desc;
    cncvImageDescriptor op_alphablend_src_fg_desc;
    cncvImageDescriptor op_alphablend_dst_desc;
    cncvRect op_alphablend_src_bg_rois;
    uint32_t alphablend_pixfmt_plane_num;

    // cncvRgbxToYuv_BasicROIP2: use for outputing yuv pixfmt
    bool rgb2yuv_enable;
    cncvImageDescriptor op_rgb2yuv_src_desc;
    cncvImageDescriptor op_rgb2yuv_dst_desc;
    cncvRect op_rgb2yux_src_rois;
    uint32_t rgb2yuv_src_pixfmt_plane_num;
    uint32_t rgb2yuv_dst_pixfmt_plane_num;

    // system
    cncvHandle_t handle;
    cnrtQueue_t  queue;
    int batch_size = 1;
    float sw_time  = 0.0;
    float hw_time  = 0.0;
    struct timeval end;
    struct timeval start;
    cnrtNotifier_t event_begin;
    cnrtNotifier_t event_end;
};

/*
 overlay_fg_pixfmt-------->|
                           |
 overlay_bg_pixfmt------------>alphablend_pixfmt-------->overlay_bg_pixfmt
*/
int mluOpOverlayInit(HANDLE *h,
        uint32_t overlay_src_bg_width,
        uint32_t overlay_src_bg_height,
        const char *overlay_bg_pixfmt,
        const char *overlay_fg_pixfmt,
        const char *alphablend_pixfmt) {
    CvOverlayPrivate *d_ptr_ = new CvOverlayPrivate;
    // system
    uint32_t plane_strides[4];
    MLUOP_RT_CHECK(mluQueueCreate(&d_ptr_->queue), "mluQueueCreate");
    MLUOP_RT_CHECK(cncvCreate(&d_ptr_->handle),    "cncvCreate");
    MLUOP_RT_CHECK(cncvSetQueue(d_ptr_->handle, d_ptr_->queue), "cncvSetQueue");

    cncvPixelFormat bg_pixfmt = getCNCVPixFmtFromPixindex(overlay_bg_pixfmt);
    cncvPixelFormat fg_pixfmt = getCNCVPixFmtFromPixindex(overlay_fg_pixfmt);
    cncvPixelFormat blend_pixfmt = getCNCVPixFmtFromPixindex(alphablend_pixfmt);

    std::string op_yuv2rgb_src_bg_pixfmt    = overlay_bg_pixfmt;
    std::string op_yuv2rgb_dst_bg_pixfmt    = alphablend_pixfmt;
    std::string op_alphablend_pixfmt        = alphablend_pixfmt;
    std::string op_rgb2yuv_src_pix_fmt      = alphablend_pixfmt;
    std::string op_rgb2yuv_dst_pix_fmt      = overlay_bg_pixfmt;
    d_ptr_->op_resize_convert_src_fg_pixfmt = overlay_fg_pixfmt;
    d_ptr_->op_resize_convert_dst_fg_pixfmt = alphablend_pixfmt;

    d_ptr_->overlay_src_bg_width  = PAD_UP(overlay_src_bg_width, ALIGN_Y2R_CVT);
    d_ptr_->overlay_src_bg_height = overlay_src_bg_height;
    d_ptr_->src_bg_colorspace     = CNCV_COLOR_SPACE_BT_601;
    d_ptr_->src_fg_colorspace     = CNCV_COLOR_SPACE_BT_601;

    if (CNCV_PIX_FMT_NV12 != bg_pixfmt && CNCV_PIX_FMT_NV21 != bg_pixfmt &&
        CNCV_PIX_FMT_BGR  != bg_pixfmt && CNCV_PIX_FMT_RGB  != bg_pixfmt &&
        CNCV_PIX_FMT_RGBA != bg_pixfmt && CNCV_PIX_FMT_BGRA != bg_pixfmt &&
        CNCV_PIX_FMT_ARGB != bg_pixfmt && CNCV_PIX_FMT_ABGR != bg_pixfmt) {
        std::cout << "background pixfmt only support nv12/nv12/rgbx, now input:"
                << overlay_bg_pixfmt << std::endl;
        throw std::runtime_error("background pixfmt input invalid");
    }
    if (CNCV_PIX_FMT_NV12 != fg_pixfmt && CNCV_PIX_FMT_NV21 != fg_pixfmt &&
        CNCV_PIX_FMT_I420 != fg_pixfmt && CNCV_PIX_FMT_BGR  != fg_pixfmt &&
        CNCV_PIX_FMT_RGB  != fg_pixfmt && CNCV_PIX_FMT_RGBA != fg_pixfmt &&
        CNCV_PIX_FMT_BGRA != fg_pixfmt && CNCV_PIX_FMT_ARGB != fg_pixfmt &&
        CNCV_PIX_FMT_ABGR != fg_pixfmt) {
        std::cout << "foreground pixfmt only support nv12/nv12/yuv420p/rgbx, now input:"
                << overlay_fg_pixfmt << std::endl;
        throw std::runtime_error("foreground pixfmt input invalid");
    }
    if (CNCV_PIX_FMT_BGR  != blend_pixfmt && CNCV_PIX_FMT_RGB  != blend_pixfmt &&
        CNCV_PIX_FMT_RGBA != blend_pixfmt && CNCV_PIX_FMT_BGRA != blend_pixfmt &&
        CNCV_PIX_FMT_ARGB != blend_pixfmt && CNCV_PIX_FMT_ABGR != blend_pixfmt) {
        std::cout << "blend pixfmt only support rgbx, now input:"
                << blend_pixfmt << std::endl;
        throw std::runtime_error("blend pixfmt input invalid");
    }
    if ((CNCV_PIX_FMT_BGR  == fg_pixfmt && fg_pixfmt != blend_pixfmt) ||
        (CNCV_PIX_FMT_RGB  == fg_pixfmt && fg_pixfmt != blend_pixfmt) ||
        (CNCV_PIX_FMT_RGBA == fg_pixfmt && fg_pixfmt != blend_pixfmt) ||
        (CNCV_PIX_FMT_BGRA == fg_pixfmt && fg_pixfmt != blend_pixfmt) ||
        (CNCV_PIX_FMT_ARGB == fg_pixfmt && fg_pixfmt != blend_pixfmt) ||
        (CNCV_PIX_FMT_ABGR == fg_pixfmt && fg_pixfmt != blend_pixfmt)) {
        std::cout << "foreground pixfmt and blend_pixfmt don't match;" << "fg_pixfmt="
                << fg_pixfmt << ", blend_pixfmt=" << blend_pixfmt << std::endl;
        throw std::runtime_error("foreground pixfmt and blend pixfmt don't match");
    }
    if ((CNCV_PIX_FMT_BGR  == bg_pixfmt && bg_pixfmt != blend_pixfmt) ||
        (CNCV_PIX_FMT_RGB  == bg_pixfmt && bg_pixfmt != blend_pixfmt) ||
        (CNCV_PIX_FMT_RGBA == bg_pixfmt && bg_pixfmt != blend_pixfmt) ||
        (CNCV_PIX_FMT_BGRA == bg_pixfmt && bg_pixfmt != blend_pixfmt) ||
        (CNCV_PIX_FMT_ARGB == bg_pixfmt && bg_pixfmt != blend_pixfmt) ||
        (CNCV_PIX_FMT_ABGR == bg_pixfmt && bg_pixfmt != blend_pixfmt)) {
        std::cout << "background pixfmt and blend_pixfmt don't match;" << "bg_pixfmt="
                << bg_pixfmt << ", blend_pixfmt=" << blend_pixfmt << std::endl;
        throw std::runtime_error("background pixfmt and blend pixfmt don't match");
    }

    d_ptr_->bg_yuv2rgb_enable = true;
    if (!strcmp(overlay_bg_pixfmt, "rgb24") || !strcmp(overlay_bg_pixfmt, "RGB24") ||
        !strcmp(overlay_bg_pixfmt, "bgr24") || !strcmp(overlay_bg_pixfmt, "BGR24") ||
        !strcmp(overlay_bg_pixfmt, "rgba")  || !strcmp(overlay_bg_pixfmt, "RGBA")  ||
        !strcmp(overlay_bg_pixfmt, "bgra")  || !strcmp(overlay_bg_pixfmt, "BGRA")  ||
        !strcmp(overlay_bg_pixfmt, "argb")  || !strcmp(overlay_bg_pixfmt, "ARGB")  ||
        !strcmp(overlay_bg_pixfmt, "abgr")  || !strcmp(overlay_bg_pixfmt, "ABGR")) {
        d_ptr_->bg_yuv2rgb_enable = false;
    }
    d_ptr_->fg_resize_convert_enable = true;
    if (!strcmp(overlay_fg_pixfmt, "rgb24") || !strcmp(overlay_fg_pixfmt, "RGB24") ||
        !strcmp(overlay_fg_pixfmt, "bgr24") || !strcmp(overlay_fg_pixfmt, "BGR24") ||
        !strcmp(overlay_fg_pixfmt, "rgba")  || !strcmp(overlay_fg_pixfmt, "RGBA")  ||
        !strcmp(overlay_fg_pixfmt, "bgra")  || !strcmp(overlay_fg_pixfmt, "BGRA")  ||
        !strcmp(overlay_fg_pixfmt, "argb")  || !strcmp(overlay_fg_pixfmt, "ARGB")  ||
        !strcmp(overlay_fg_pixfmt, "abgr")  || !strcmp(overlay_fg_pixfmt, "ABGR")) {
        d_ptr_->fg_resize_convert_enable = false;
    }
    d_ptr_->rgb2yuv_enable = d_ptr_->bg_yuv2rgb_enable;
    if (d_ptr_->rgb2yuv_enable &&
        CNCV_PIX_FMT_NV12 != bg_pixfmt && CNCV_PIX_FMT_NV21 != bg_pixfmt) {
        throw std::runtime_error("mluOverlay only support nv12/nv21, when input yuv pixfmt");
    }
    if (!d_ptr_->bg_yuv2rgb_enable && strcmp(alphablend_pixfmt, overlay_bg_pixfmt)) {
        throw std::runtime_error("when background input rgbx, must be equal to blend pixfmt");
    }
    if (!d_ptr_->fg_resize_convert_enable && strcmp(alphablend_pixfmt, overlay_fg_pixfmt)) {
        throw std::runtime_error("when foreground input rgbx, must be equal to blend pixfmt");
    }
    if (!d_ptr_->bg_yuv2rgb_enable && !d_ptr_->fg_resize_convert_enable &&
        (strcmp(alphablend_pixfmt, overlay_bg_pixfmt) ||
        strcmp(alphablend_pixfmt, overlay_fg_pixfmt)  ||
        strcmp(overlay_bg_pixfmt, overlay_fg_pixfmt))) {
        throw std::runtime_error("when fg and bg input rgbx, must be equal to blend pixfmt");
    }
    d_ptr_->yuv2rgb_src_pixfmt_plane_num = getPlaneNumFromPixfmt(bg_pixfmt);
    d_ptr_->yuv2rgb_dst_pixfmt_plane_num = getPlaneNumFromPixfmt(blend_pixfmt);
    d_ptr_->alphablend_pixfmt_plane_num  = d_ptr_->yuv2rgb_dst_pixfmt_plane_num;
    d_ptr_->rgb2yuv_src_pixfmt_plane_num = d_ptr_->yuv2rgb_dst_pixfmt_plane_num;
    d_ptr_->rgb2yuv_dst_pixfmt_plane_num = d_ptr_->yuv2rgb_src_pixfmt_plane_num;
    d_ptr_->resize_convert_dst_pixfmt_plane_num = d_ptr_->alphablend_pixfmt_plane_num;
    d_ptr_->resize_convert_src_pixfmt_plane_num = getPlaneNumFromPixfmt(fg_pixfmt);

    d_ptr_->src_bg_pixfmt_plane_num = getPlaneNumFromPixfmt(bg_pixfmt);
    d_ptr_->src_fg_pixfmt_plane_num = getPlaneNumFromPixfmt(fg_pixfmt);
    d_ptr_->dst_pixfmt_plane_num    = d_ptr_->src_bg_pixfmt_plane_num;

    /*********************************** cncvYuvToRgbx_V2 init ***********************************/
    // support multi batch, need trans to one batch, for bg images
    if (d_ptr_->bg_yuv2rgb_enable) {
        d_ptr_->op_yuv2rgb_src_bg_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size * d_ptr_->yuv2rgb_src_pixfmt_plane_num));
        d_ptr_->op_yuv2rgb_dst_bg_ptrs_cpu = reinterpret_cast<void **>(malloc(sizeof(char*) * d_ptr_->batch_size));
        // for mlu data address array
        MLUOP_RT_CHECK(
        cnrtMalloc((void **)(&d_ptr_->op_yuv2rgb_src_bg_ptrs_mlu), d_ptr_->batch_size * d_ptr_->yuv2rgb_src_pixfmt_plane_num * sizeof(void*)), "cnrtMalloc");
        MLUOP_RT_CHECK(
        cnrtMalloc((void **)(&d_ptr_->op_yuv2rgb_dst_bg_ptrs_mlu), d_ptr_->batch_size * sizeof(void*)), "cnrtMalloc");

        memset(&d_ptr_->op_yuv2rgb_src_bg_desc, 0, sizeof(d_ptr_->op_yuv2rgb_src_bg_desc));
        d_ptr_->op_yuv2rgb_src_bg_desc.width     = PAD_UP(overlay_src_bg_width, ALIGN_Y2R_CVT);
        d_ptr_->op_yuv2rgb_src_bg_desc.height    = overlay_src_bg_height;
        d_ptr_->op_yuv2rgb_src_bg_desc.pixel_fmt = bg_pixfmt;
        getPlaneStrideFromPixfmt(plane_strides,
                d_ptr_->op_yuv2rgb_src_bg_desc.pixel_fmt, d_ptr_->op_yuv2rgb_src_bg_desc.width, CNCV_DEPTH_8U, ALIGN_Y2R_CVT);
        d_ptr_->op_yuv2rgb_src_bg_desc.stride[0] = plane_strides[0];
        d_ptr_->op_yuv2rgb_src_bg_desc.stride[1] = plane_strides[1];
        d_ptr_->op_yuv2rgb_src_bg_desc.stride[2] = plane_strides[2];
        d_ptr_->op_yuv2rgb_src_bg_desc.stride[3] = plane_strides[3];
        d_ptr_->op_yuv2rgb_src_bg_desc.depth     = CNCV_DEPTH_8U;
        d_ptr_->op_yuv2rgb_src_bg_desc.color_space = d_ptr_->src_bg_colorspace;
        memset(&d_ptr_->op_yuv2rgb_dst_bg_desc, 0, sizeof(d_ptr_->op_yuv2rgb_dst_bg_desc));
        d_ptr_->op_yuv2rgb_dst_bg_desc.width  = PAD_UP(overlay_src_bg_width, ALIGN_Y2R_CVT);
        d_ptr_->op_yuv2rgb_dst_bg_desc.height = overlay_src_bg_height;
        d_ptr_->op_yuv2rgb_dst_bg_desc.pixel_fmt = blend_pixfmt;
        getPlaneStrideFromPixfmt(plane_strides,
                d_ptr_->op_yuv2rgb_dst_bg_desc.pixel_fmt, d_ptr_->op_yuv2rgb_dst_bg_desc.width, CNCV_DEPTH_8U, ALIGN_Y2R_CVT);
        d_ptr_->op_yuv2rgb_dst_bg_desc.stride[0] = plane_strides[0];
        d_ptr_->op_yuv2rgb_dst_bg_desc.depth     = CNCV_DEPTH_8U;
        d_ptr_->op_yuv2rgb_dst_bg_size = d_ptr_->op_yuv2rgb_dst_bg_desc.stride[0] * d_ptr_->op_yuv2rgb_dst_bg_desc.height;
        MLUOP_RT_CHECK( // alloc yuv2rgb dst and blend src bg
        cnrtMalloc((void **)(&d_ptr_->op_yuv2rgb_dst_blend_src_bg_ptr_mlu), d_ptr_->op_yuv2rgb_dst_bg_size), "cnrtMalloc");
    }
    /************************* cncvResizeConvertApply_AdvancedROI init **************************/
    // support multi batch, need trans to one batch, for fg images
    // for getting max rc workspace size, suppose processing 64*64--> bg_w*bg_h
    if (d_ptr_->fg_resize_convert_enable) {
        d_ptr_->last_roi_w = overlay_src_bg_width;
        d_ptr_->last_roi_h = overlay_src_bg_height;
        d_ptr_->op_resize_convert_input_w  = 64; // for getting probe workspace size
        d_ptr_->op_resize_convert_input_h  = 64;
        d_ptr_->op_resize_convert_output_w = PAD_UP(d_ptr_->last_roi_w, ALIGN_Y2R_CVT);
        d_ptr_->op_resize_convert_output_h = d_ptr_->last_roi_h;

        d_ptr_->op_resize_convert_src_rois.h = d_ptr_->op_resize_convert_input_h;
        d_ptr_->op_resize_convert_src_rois.w = d_ptr_->op_resize_convert_input_w;
        d_ptr_->op_resize_convert_src_rois.x = 0;
        d_ptr_->op_resize_convert_src_rois.y = 0;
        d_ptr_->op_resize_convert_dst_rois.h = d_ptr_->op_resize_convert_output_h;
        d_ptr_->op_resize_convert_dst_rois.w = d_ptr_->op_resize_convert_output_w;
        d_ptr_->op_resize_convert_dst_rois.x = 0;
        d_ptr_->op_resize_convert_dst_rois.y = 0;
        memset(&d_ptr_->op_resize_convert_src_desc, 0, sizeof(d_ptr_->op_resize_convert_src_desc));
        d_ptr_->op_resize_convert_src_desc.width     = d_ptr_->op_resize_convert_input_w;
        d_ptr_->op_resize_convert_src_desc.height    = d_ptr_->op_resize_convert_input_h;
        d_ptr_->op_resize_convert_src_desc.pixel_fmt = fg_pixfmt;
        getPlaneStrideFromPixfmt(plane_strides,
                d_ptr_->op_resize_convert_src_desc.pixel_fmt, d_ptr_->op_resize_convert_src_desc.width, CNCV_DEPTH_8U, ALIGN_Y2R_CVT);
        d_ptr_->op_resize_convert_src_desc.stride[0] = plane_strides[0];
        d_ptr_->op_resize_convert_src_desc.stride[1] = plane_strides[1];
        d_ptr_->op_resize_convert_src_desc.stride[2] = plane_strides[2];
        d_ptr_->op_resize_convert_src_desc.stride[3] = plane_strides[3];
        d_ptr_->op_resize_convert_src_desc.depth     = CNCV_DEPTH_8U;
        d_ptr_->op_resize_convert_src_desc.color_space = d_ptr_->src_fg_colorspace;
        memset(&d_ptr_->op_resize_convert_dst_desc, 0, sizeof(d_ptr_->op_resize_convert_dst_desc));
        d_ptr_->op_resize_convert_dst_desc.width     = d_ptr_->op_resize_convert_output_w;
        d_ptr_->op_resize_convert_dst_desc.height    = d_ptr_->op_resize_convert_output_h;
        d_ptr_->op_resize_convert_dst_desc.pixel_fmt = blend_pixfmt;
        getPlaneStrideFromPixfmt(plane_strides,
                d_ptr_->op_resize_convert_dst_desc.pixel_fmt, d_ptr_->op_resize_convert_dst_desc.width, CNCV_DEPTH_8U, ALIGN_Y2R_CVT);
        d_ptr_->op_resize_convert_dst_desc.stride[0] = plane_strides[0];
        d_ptr_->op_resize_convert_dst_desc.depth     = CNCV_DEPTH_8U;
        d_ptr_->op_resize_convert_dst_desc.color_space = d_ptr_->src_fg_colorspace;
        // for mlu data address array
        d_ptr_->op_resize_convert_src_fg_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(void*) *
                d_ptr_->batch_size * d_ptr_->resize_convert_src_pixfmt_plane_num));
        d_ptr_->op_resize_convert_dst_fg_ptrs_cpu_ = reinterpret_cast<void **>(malloc(sizeof(void*) * d_ptr_->batch_size));
        MLUOP_RT_CHECK(cnrtMalloc((void **)(&d_ptr_->op_resize_convert_src_fg_ptrs_mlu_),
                d_ptr_->batch_size * d_ptr_->resize_convert_src_pixfmt_plane_num * sizeof(void*)), "cnrtMalloc");
        MLUOP_RT_CHECK(cnrtMalloc((void **)(&d_ptr_->op_resize_convert_dst_fg_ptrs_mlu_), d_ptr_->batch_size * sizeof(void*)), "cnrtMalloc");
        // alloc resize convert dst and blend src fg, probe max workspace size
        int resize_convert_out_size = d_ptr_->op_resize_convert_dst_desc.stride[0] * d_ptr_->op_resize_convert_output_h;
        MLUOP_RT_CHECK(cnrtMalloc((void **)(&d_ptr_->op_resize_convert_dst_blend_src_fg_ptr_mlu), resize_convert_out_size), "cnrtMalloc");
        // init resize convert op
        MLUOP_CV_CHECK(cncvResizeConvertCreate(d_ptr_->handle, CNCV_INTER_BILINEAR, &d_ptr_->rc_op), "cncvResizeConvertCreate");
        MLUOP_CV_CHECK(cncvResizeConvertSetOp_AdvancedROI(d_ptr_->rc_op, d_ptr_->batch_size,
                const_cast<cncvImageDescriptor*>(&d_ptr_->op_resize_convert_src_desc),
                const_cast<cncvRect*>(&d_ptr_->op_resize_convert_src_rois),
                const_cast<cncvImageDescriptor*>(&d_ptr_->op_resize_convert_dst_desc),
                const_cast<cncvRect*>(&d_ptr_->op_resize_convert_dst_rois)),
                "cncvResizeConvertSetOp_AdvancedROI");
        MLUOP_CV_CHECK(cncvResizeConvertGetAuxDataSize(d_ptr_->rc_op, &d_ptr_->probe_resize_convert_workspace_size), "cncvResizeConvertGetAuxDataSize");
        d_ptr_->op_resize_convert_workspace_cpu = malloc(d_ptr_->probe_resize_convert_workspace_size);
        MLUOP_RT_CHECK(cnrtMalloc((void **)(&d_ptr_->op_resize_convert_workspace_mlu),
                d_ptr_->probe_resize_convert_workspace_size), "cnrtMalloc");
        MLUOP_CV_CHECK(cncvResizeConvertInitAuxData(d_ptr_->rc_op, d_ptr_->op_resize_convert_workspace_cpu), "cncvResizeConvertInitAuxData");
        MLUOP_CV_CHECK(cnrtMemcpy(d_ptr_->op_resize_convert_workspace_mlu, d_ptr_->op_resize_convert_workspace_cpu,
                d_ptr_->probe_resize_convert_workspace_size, CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
    }

    /****************************** cncvAlphaBlend_BasicROI init ********************************/
    // only support one batch; ptr mlu: means real mlu data address
    if (d_ptr_->rgb2yuv_enable) {
        MLUOP_RT_CHECK(
        cnrtMalloc((void **)(&d_ptr_->op_alphablend_dst_ptr_mlu), d_ptr_->op_yuv2rgb_dst_bg_size), "cnrtMalloc");
    }
    memset(&d_ptr_->op_alphablend_src_bg_desc, 0, sizeof(d_ptr_->op_alphablend_src_bg_desc));
    d_ptr_->op_alphablend_src_bg_desc.width  = d_ptr_->overlay_src_bg_width;
    d_ptr_->op_alphablend_src_bg_desc.height = d_ptr_->overlay_src_bg_height;
    d_ptr_->op_alphablend_src_bg_desc.pixel_fmt = blend_pixfmt;
    getPlaneStrideFromPixfmt(plane_strides,
            d_ptr_->op_alphablend_src_bg_desc.pixel_fmt, d_ptr_->op_alphablend_src_bg_desc.width, CNCV_DEPTH_8U, ALIGN_Y2R_CVT);
    d_ptr_->op_alphablend_src_bg_desc.stride[0] = plane_strides[0];
    d_ptr_->op_alphablend_src_bg_desc.depth     = CNCV_DEPTH_8U;
    d_ptr_->op_alphablend_src_bg_desc.color_space = d_ptr_->src_bg_colorspace;
    memset(&d_ptr_->op_alphablend_dst_desc,   0, sizeof(d_ptr_->op_alphablend_dst_desc));
    d_ptr_->op_alphablend_dst_desc.width  = d_ptr_->overlay_src_bg_width;
    d_ptr_->op_alphablend_dst_desc.height = d_ptr_->overlay_src_bg_height;
    d_ptr_->op_alphablend_dst_desc.pixel_fmt = blend_pixfmt;
    getPlaneStrideFromPixfmt(plane_strides,
            d_ptr_->op_alphablend_dst_desc.pixel_fmt, d_ptr_->op_alphablend_dst_desc.width, CNCV_DEPTH_8U, ALIGN_Y2R_CVT);
    d_ptr_->op_alphablend_dst_desc.stride[0] =  plane_strides[0];
    d_ptr_->op_alphablend_dst_desc.depth     = CNCV_DEPTH_8U;
    d_ptr_->op_alphablend_dst_desc.color_space = d_ptr_->src_bg_colorspace;

    /****************************** cncvRgbxToYuv_BasicROIP2 init ********************************/
    // only support one batch
    if (d_ptr_->rgb2yuv_enable) {
        memset(&d_ptr_->op_rgb2yuv_src_desc, 0, sizeof(d_ptr_->op_rgb2yuv_src_desc));
        d_ptr_->op_rgb2yuv_src_desc.width  = d_ptr_->overlay_src_bg_width;
        d_ptr_->op_rgb2yuv_src_desc.height = d_ptr_->overlay_src_bg_height;
        d_ptr_->op_rgb2yuv_src_desc.pixel_fmt = blend_pixfmt;
        getPlaneStrideFromPixfmt(plane_strides,
                d_ptr_->op_rgb2yuv_src_desc.pixel_fmt, d_ptr_->op_rgb2yuv_src_desc.width, CNCV_DEPTH_8U, ALIGN_R2Y_CVT);
        d_ptr_->op_rgb2yuv_src_desc.stride[0] = plane_strides[0];
        d_ptr_->op_rgb2yuv_src_desc.depth     = CNCV_DEPTH_8U;
        d_ptr_->op_rgb2yuv_src_desc.color_space = d_ptr_->src_bg_colorspace;

        memset(&d_ptr_->op_rgb2yuv_dst_desc, 0, sizeof(d_ptr_->op_rgb2yuv_dst_desc));
        d_ptr_->op_rgb2yuv_dst_desc.width  = d_ptr_->overlay_src_bg_width;
        d_ptr_->op_rgb2yuv_dst_desc.height = d_ptr_->overlay_src_bg_height;
        d_ptr_->op_rgb2yuv_dst_desc.pixel_fmt = bg_pixfmt;
        getPlaneStrideFromPixfmt(plane_strides,
                d_ptr_->op_rgb2yuv_dst_desc.pixel_fmt, d_ptr_->op_rgb2yuv_dst_desc.width, CNCV_DEPTH_8U, ALIGN_R2Y_CVT);
        d_ptr_->op_rgb2yuv_dst_desc.stride[0] = plane_strides[0];
        d_ptr_->op_rgb2yuv_dst_desc.stride[1] = plane_strides[1];
        d_ptr_->op_rgb2yuv_dst_desc.stride[2] = plane_strides[2];
        d_ptr_->op_rgb2yuv_dst_desc.stride[3] = plane_strides[3];
        d_ptr_->op_rgb2yuv_dst_desc.depth     = CNCV_DEPTH_8U;
        d_ptr_->op_rgb2yuv_dst_desc.color_space = d_ptr_->src_bg_colorspace;
        d_ptr_->op_rgb2yux_src_rois = {0, 0, (PAD_UP(overlay_src_bg_width, ALIGN_R2Y_CVT)), overlay_src_bg_height};
    }

    // build handle
    *h = static_cast<void *>(d_ptr_);

    return 0;
}

int mluOpOverlayExec(HANDLE h, void* dst_addr, void* src_bg_addr, void* src_fg_addr,
        uint32_t fg_src_w,    uint32_t fg_src_h,
        uint32_t blend_roi_x, uint32_t blend_roi_y,
        uint32_t blend_roi_w, uint32_t blend_roi_h,
        float alpha = 0.8, float beta = 0.2, float gamma = 0.0) {
    CvOverlayPrivate *d_ptr_ = static_cast<CvOverlayPrivate *>(h);
    if (nullptr == d_ptr_->queue) {
        throw std::runtime_error("queue is not created");
        return -1;
    }
    if (blend_roi_w > d_ptr_->overlay_src_bg_width || blend_roi_h > d_ptr_->overlay_src_bg_height ||
        fg_src_w > d_ptr_->overlay_src_bg_width || fg_src_h > d_ptr_->overlay_src_bg_height) {
        throw std::runtime_error("mluOverlay foreground size exceed background image size");
        return -1;
    }
    uint32_t plane_strides[4] = {0};
    void *src_bg_ptr[4], *src_fg_ptr[4], *dst_ptr[4];
    for (int i = 0; i < d_ptr_->src_bg_pixfmt_plane_num; i++) {
        src_bg_ptr[i] = ((void**)src_bg_addr)[i];
    }
    for (int i = 0; i < d_ptr_->src_fg_pixfmt_plane_num; i++) {
        src_fg_ptr[i] = ((void**)src_fg_addr)[i];
    }
    for (int i = 0; i < d_ptr_->dst_pixfmt_plane_num; i++) {
        dst_ptr[i] = ((void**)(dst_addr))[i];
    }
    // op resize and convert
    if (d_ptr_->fg_resize_convert_enable) {
        for (uint32_t bi = 0; bi < d_ptr_->batch_size; ++bi) {
            for (int i = 0; i < d_ptr_->src_fg_pixfmt_plane_num; i++) {
                d_ptr_->op_resize_convert_src_fg_ptrs_cpu_[d_ptr_->src_fg_pixfmt_plane_num * bi + i] = src_fg_ptr[i];
            }
            d_ptr_->op_resize_convert_dst_fg_ptrs_cpu_[bi] = d_ptr_->op_resize_convert_dst_blend_src_fg_ptr_mlu;
        }
        MLUOP_RT_CHECK(cnrtMemcpyAsync(d_ptr_->op_resize_convert_src_fg_ptrs_mlu_,
                d_ptr_->op_resize_convert_src_fg_ptrs_cpu_,
                sizeof(void*) * d_ptr_->batch_size * d_ptr_->resize_convert_src_pixfmt_plane_num,
                d_ptr_->queue, CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
        MLUOP_RT_CHECK(cnrtMemcpyAsync(d_ptr_->op_resize_convert_dst_fg_ptrs_mlu_,
                d_ptr_->op_resize_convert_dst_fg_ptrs_cpu_, sizeof(void*) * d_ptr_->batch_size,
                d_ptr_->queue, CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
        if (d_ptr_->op_resize_convert_input_w  != fg_src_w ||
            d_ptr_->op_resize_convert_input_h  != fg_src_h ||
            d_ptr_->op_resize_convert_output_w != blend_roi_w ||
            d_ptr_->op_resize_convert_output_h != blend_roi_h ||
            d_ptr_->last_roi_w != blend_roi_w || d_ptr_->last_roi_h != blend_roi_h) {
            /****** cncvResizeConvertApply_AdvancedROI init ******/
            // support multi batch, need trans to one batch, for fg images
            d_ptr_->op_resize_convert_input_w  = PAD_UP(fg_src_w, ALIGN_Y2R_CVT);
            d_ptr_->op_resize_convert_input_h  = fg_src_h;
            d_ptr_->op_resize_convert_output_w = PAD_UP(blend_roi_w, ALIGN_Y2R_CVT);
            d_ptr_->op_resize_convert_output_h = blend_roi_h;
            d_ptr_->last_roi_w = blend_roi_w;
            d_ptr_->last_roi_h = blend_roi_h;

            d_ptr_->op_resize_convert_src_rois.h = d_ptr_->op_resize_convert_input_h;
            d_ptr_->op_resize_convert_src_rois.w = d_ptr_->op_resize_convert_input_w;
            d_ptr_->op_resize_convert_src_rois.x = 0;
            d_ptr_->op_resize_convert_src_rois.y = 0;
            d_ptr_->op_resize_convert_dst_rois.h = d_ptr_->op_resize_convert_output_h;
            d_ptr_->op_resize_convert_dst_rois.w = d_ptr_->op_resize_convert_output_w;
            d_ptr_->op_resize_convert_dst_rois.x = 0;
            d_ptr_->op_resize_convert_dst_rois.y = 0;
            memset(&d_ptr_->op_resize_convert_src_desc, 0, sizeof(d_ptr_->op_resize_convert_src_desc));
            d_ptr_->op_resize_convert_src_desc.width     = d_ptr_->op_resize_convert_input_w;
            d_ptr_->op_resize_convert_src_desc.height    = d_ptr_->op_resize_convert_input_h;
            d_ptr_->op_resize_convert_src_desc.pixel_fmt = getCNCVPixFmtFromPixindex(d_ptr_->op_resize_convert_src_fg_pixfmt.c_str());
            getPlaneStrideFromPixfmt(plane_strides,
                    d_ptr_->op_resize_convert_src_desc.pixel_fmt, d_ptr_->op_resize_convert_src_desc.width, CNCV_DEPTH_8U, ALIGN_Y2R_CVT);
            d_ptr_->op_resize_convert_src_desc.stride[0] = plane_strides[0];
            d_ptr_->op_resize_convert_src_desc.stride[1] = plane_strides[1];
            d_ptr_->op_resize_convert_src_desc.stride[2] = plane_strides[2];
            d_ptr_->op_resize_convert_src_desc.stride[3] = plane_strides[3];
            d_ptr_->op_resize_convert_src_desc.depth     = CNCV_DEPTH_8U;
            d_ptr_->op_resize_convert_src_desc.color_space = d_ptr_->src_fg_colorspace;
            memset(&d_ptr_->op_resize_convert_dst_desc, 0, sizeof(d_ptr_->op_resize_convert_dst_desc));
            d_ptr_->op_resize_convert_dst_desc.width     = d_ptr_->op_resize_convert_output_w;
            d_ptr_->op_resize_convert_dst_desc.height    = d_ptr_->op_resize_convert_output_h;
            d_ptr_->op_resize_convert_dst_desc.pixel_fmt = getCNCVPixFmtFromPixindex(d_ptr_->op_resize_convert_dst_fg_pixfmt.c_str());
            getPlaneStrideFromPixfmt(plane_strides,
                    d_ptr_->op_resize_convert_dst_desc.pixel_fmt, d_ptr_->op_resize_convert_dst_desc.width, CNCV_DEPTH_8U, ALIGN_Y2R_CVT);
            d_ptr_->op_resize_convert_dst_desc.stride[0] = plane_strides[0];
            d_ptr_->op_resize_convert_dst_desc.depth     = CNCV_DEPTH_8U;
            d_ptr_->op_resize_convert_dst_desc.color_space = d_ptr_->src_fg_colorspace;
            // MLUOP_CV_CHECK(cncvResizeConvertCreate(d_ptr_->handle, CNCV_INTER_BILINEAR, &d_ptr_->rc_op), "cncvResizeConvertCreate");
            MLUOP_CV_CHECK(cncvResizeConvertSetOp_AdvancedROI(d_ptr_->rc_op, d_ptr_->batch_size,
                    const_cast<cncvImageDescriptor*>(&d_ptr_->op_resize_convert_src_desc),
                    const_cast<cncvRect*>(&d_ptr_->op_resize_convert_src_rois),
                    const_cast<cncvImageDescriptor*>(&d_ptr_->op_resize_convert_dst_desc),
                    const_cast<cncvRect*>(&d_ptr_->op_resize_convert_dst_rois)),
                    "cncvResizeConvertSetOp_AdvancedROI");
            size_t resize_convert_workspace_size = 0;
            MLUOP_CV_CHECK(cncvResizeConvertGetAuxDataSize(d_ptr_->rc_op, &resize_convert_workspace_size), "cncvResizeConvertGetAuxDataSize");
            if (resize_convert_workspace_size > d_ptr_->probe_resize_convert_workspace_size) {
                fprintf(stderr, "Error: rc new workspace size more than probe rc workspace size, func:%s, line:%d\n", __func__, __LINE__);
                return -1;
            }
            MLUOP_CV_CHECK(cncvResizeConvertInitAuxData(d_ptr_->rc_op, d_ptr_->op_resize_convert_workspace_cpu), "cncvResizeConvertInitAuxData");
            MLUOP_CV_CHECK(cnrtMemcpyAsync(d_ptr_->op_resize_convert_workspace_mlu,
                    d_ptr_->op_resize_convert_workspace_cpu, resize_convert_workspace_size,
                    d_ptr_->queue, CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
        }
        MLUOP_CV_CHECK(cncvResizeConvertApply_AdvancedROI(d_ptr_->rc_op,
                reinterpret_cast<cncvBufferList_t>(d_ptr_->op_resize_convert_src_fg_ptrs_mlu_),
                reinterpret_cast<cncvBufferList_t>(d_ptr_->op_resize_convert_dst_fg_ptrs_mlu_),
                d_ptr_->op_resize_convert_workspace_mlu), "cncvResizeConvertApply_AdvancedROI");
    }

    // op yuv2rgb
    if (d_ptr_->bg_yuv2rgb_enable) {
        for (int bi = 0; bi < d_ptr_->batch_size; ++bi) {
            for (int i = 0; i < d_ptr_->src_bg_pixfmt_plane_num; i++) {
                d_ptr_->op_yuv2rgb_src_bg_ptrs_cpu[d_ptr_->src_bg_pixfmt_plane_num * bi + i] = src_bg_ptr[i];
            }
            d_ptr_->op_yuv2rgb_dst_bg_ptrs_cpu[bi] = d_ptr_->op_yuv2rgb_dst_blend_src_bg_ptr_mlu;
        }
        MLUOP_RT_CHECK(cnrtMemcpyAsync(d_ptr_->op_yuv2rgb_src_bg_ptrs_mlu,
                d_ptr_->op_yuv2rgb_src_bg_ptrs_cpu,
                sizeof(void*) * d_ptr_->batch_size * d_ptr_->yuv2rgb_src_pixfmt_plane_num,
                d_ptr_->queue, CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
        MLUOP_RT_CHECK(cnrtMemcpyAsync(d_ptr_->op_yuv2rgb_dst_bg_ptrs_mlu,
                d_ptr_->op_yuv2rgb_dst_bg_ptrs_cpu, sizeof(void*) * d_ptr_->batch_size,
                d_ptr_->queue, CNRT_MEM_TRANS_DIR_HOST2DEV), "cnrtMemcpy");
        MLUOP_CV_CHECK(cncvYuvToRgbx_V2(d_ptr_->handle,
                d_ptr_->batch_size,
                d_ptr_->op_yuv2rgb_src_bg_desc,
                reinterpret_cast<cncvBufferList_t>(d_ptr_->op_yuv2rgb_src_bg_ptrs_mlu),
                d_ptr_->op_yuv2rgb_dst_bg_desc,
                reinterpret_cast<cncvBufferList_t>(d_ptr_->op_yuv2rgb_dst_bg_ptrs_mlu)),
                "cncvYuvToRgbx");
    }

    // op cncvAlphaBlend_BasicROI
    memset(&d_ptr_->op_alphablend_src_fg_desc, 0, sizeof(d_ptr_->op_alphablend_src_fg_desc));
    d_ptr_->op_alphablend_src_fg_desc.width  = d_ptr_->fg_resize_convert_enable? d_ptr_->op_resize_convert_output_w : blend_roi_w;
    d_ptr_->op_alphablend_src_fg_desc.height = d_ptr_->fg_resize_convert_enable? d_ptr_->op_resize_convert_output_h : blend_roi_h;
    d_ptr_->op_alphablend_src_fg_desc.pixel_fmt = getCNCVPixFmtFromPixindex(d_ptr_->op_resize_convert_dst_fg_pixfmt.c_str());
    getPlaneStrideFromPixfmt(plane_strides,
            d_ptr_->op_alphablend_src_fg_desc.pixel_fmt, d_ptr_->op_alphablend_src_fg_desc.width, CNCV_DEPTH_8U, ALIGN_Y2R_CVT);
    d_ptr_->op_alphablend_src_fg_desc.stride[0] = plane_strides[0];
    d_ptr_->op_alphablend_src_fg_desc.color_space = d_ptr_->src_fg_colorspace;
    memset(&d_ptr_->op_alphablend_src_bg_rois, 0, sizeof(d_ptr_->op_alphablend_src_bg_rois));
    d_ptr_->op_alphablend_src_bg_rois.x = blend_roi_x;
    d_ptr_->op_alphablend_src_bg_rois.y = blend_roi_y;
    d_ptr_->op_alphablend_src_bg_rois.w = blend_roi_w;
    d_ptr_->op_alphablend_src_bg_rois.h = blend_roi_h;

    MLUOP_CV_CHECK(cncvAlphaBlend_BasicROI(d_ptr_->handle,
            d_ptr_->op_alphablend_src_bg_desc,
            d_ptr_->op_alphablend_src_bg_rois,
            d_ptr_->bg_yuv2rgb_enable ? d_ptr_->op_yuv2rgb_dst_blend_src_bg_ptr_mlu : src_bg_ptr[0],
            alpha,
            d_ptr_->op_alphablend_src_fg_desc,
            d_ptr_->fg_resize_convert_enable ? d_ptr_->op_resize_convert_dst_blend_src_fg_ptr_mlu : src_fg_ptr[0],
            beta,
            gamma,
            d_ptr_->op_alphablend_dst_desc,
            d_ptr_->rgb2yuv_enable ? d_ptr_->op_alphablend_dst_ptr_mlu : dst_ptr[0]),
            "cncvOverlay_BasicROI");

    // op rgbx to yuv
    if (d_ptr_->rgb2yuv_enable) {
        MLUOP_CV_CHECK(cncvRgbxToYuv_BasicROIP2(d_ptr_->handle,
                d_ptr_->op_rgb2yuv_src_desc,
                d_ptr_->op_rgb2yux_src_rois,
                reinterpret_cast<const void *>(d_ptr_->op_alphablend_dst_ptr_mlu),
                d_ptr_->op_rgb2yuv_dst_desc,
                reinterpret_cast<void *>(dst_ptr[0]),
                reinterpret_cast<void *>(dst_ptr[1])),
                "cncvRbgxToYuv_BasicROI2");
    }
    MLUOP_CV_CHECK(cncvSyncQueue(d_ptr_->handle), "cncvSyncQueue");

    return 0;
}

int mluOpOverlayDestroy(HANDLE h) {
    CvOverlayPrivate *d_ptr_ = static_cast<CvOverlayPrivate *>(h);
    if (!d_ptr_) {
        printf("mluop alphablend op not init\n");
        return -1;
    }
    // mlu memory destory
    if (d_ptr_->bg_yuv2rgb_enable) {
        if (d_ptr_->op_yuv2rgb_src_bg_ptrs_mlu) {
            MLUOP_RT_CHECK(cnrtFree(d_ptr_->op_yuv2rgb_src_bg_ptrs_mlu), "cnrtFree");
            d_ptr_->op_yuv2rgb_src_bg_ptrs_mlu = nullptr;
        }
        if (d_ptr_->op_yuv2rgb_dst_bg_ptrs_mlu) {
            MLUOP_RT_CHECK(cnrtFree(d_ptr_->op_yuv2rgb_dst_bg_ptrs_mlu), "cnrtFree");
            d_ptr_->op_yuv2rgb_dst_bg_ptrs_mlu = nullptr;
        }
        if (d_ptr_->op_yuv2rgb_dst_blend_src_bg_ptr_mlu) {
            MLUOP_RT_CHECK(cnrtFree(d_ptr_->op_yuv2rgb_dst_blend_src_bg_ptr_mlu), "cnrtFree");
            d_ptr_->op_yuv2rgb_dst_blend_src_bg_ptr_mlu = nullptr;
        }
        // cpu memory destory
        if (d_ptr_->op_yuv2rgb_src_bg_ptrs_cpu) {
            free(d_ptr_->op_yuv2rgb_src_bg_ptrs_cpu);
            d_ptr_->op_yuv2rgb_src_bg_ptrs_cpu = nullptr;
        }
        if (d_ptr_->op_yuv2rgb_dst_bg_ptrs_cpu) {
            free(d_ptr_->op_yuv2rgb_dst_bg_ptrs_cpu);
            d_ptr_->op_yuv2rgb_dst_bg_ptrs_cpu = nullptr;
        }
    }
    if (d_ptr_->rgb2yuv_enable) {
        if (d_ptr_->op_alphablend_dst_ptr_mlu) {
            MLUOP_RT_CHECK(cnrtFree(d_ptr_->op_alphablend_dst_ptr_mlu), "cnrtFree");
            d_ptr_->op_alphablend_dst_ptr_mlu = nullptr;
        }
    }
    if (d_ptr_->fg_resize_convert_enable) {
        if (d_ptr_->op_resize_convert_src_fg_ptrs_mlu_) {
            MLUOP_RT_CHECK(cnrtFree(d_ptr_->op_resize_convert_src_fg_ptrs_mlu_), "cnrtFree");
            d_ptr_->op_resize_convert_src_fg_ptrs_mlu_ = nullptr;
        }
        if (d_ptr_->op_resize_convert_dst_fg_ptrs_mlu_) {
            MLUOP_RT_CHECK(cnrtFree(d_ptr_->op_resize_convert_dst_fg_ptrs_mlu_), "cnrtFree");
            d_ptr_->op_resize_convert_dst_fg_ptrs_mlu_ = nullptr;
        }
        if (d_ptr_->op_resize_convert_dst_blend_src_fg_ptr_mlu) {
            MLUOP_RT_CHECK(cnrtFree(d_ptr_->op_resize_convert_dst_blend_src_fg_ptr_mlu), "cnrtFree");
            d_ptr_->op_resize_convert_dst_blend_src_fg_ptr_mlu = nullptr;
        }
        if (d_ptr_->op_resize_convert_workspace_mlu) {
            MLUOP_RT_CHECK(cnrtFree(d_ptr_->op_resize_convert_workspace_mlu), "cnrtFree");
            d_ptr_->op_resize_convert_workspace_mlu = nullptr;
        }
        // cpu memory destory
        if (d_ptr_->op_resize_convert_src_fg_ptrs_cpu_) {
            free(d_ptr_->op_resize_convert_src_fg_ptrs_cpu_);
            d_ptr_->op_resize_convert_src_fg_ptrs_cpu_ = nullptr;
        }
        if (d_ptr_->op_resize_convert_dst_fg_ptrs_cpu_) {
            free(d_ptr_->op_resize_convert_dst_fg_ptrs_cpu_);
            d_ptr_->op_resize_convert_dst_fg_ptrs_cpu_ = nullptr;
        }
        if (d_ptr_->op_resize_convert_workspace_cpu) {
            free(d_ptr_->op_resize_convert_workspace_cpu);
            d_ptr_->op_resize_convert_workspace_cpu = nullptr;
        }
        if (d_ptr_->rc_op) {
            MLUOP_CV_CHECK(cncvResizeConvertDestroy(d_ptr_->rc_op), "cncvResizeConvertDestroy");
            d_ptr_->rc_op = nullptr;
        }
    }
    // system destory
    if (d_ptr_->queue) {
        MLUOP_RT_CHECK(mluQueueDestroy(d_ptr_->queue), "mluQueueDestroy");
        d_ptr_->queue = nullptr;
    }
    if (d_ptr_->handle) {
        MLUOP_CV_CHECK(cncvDestroy(d_ptr_->handle), "cncvDestroy");
        d_ptr_->handle = nullptr;
    }
    delete d_ptr_;
    d_ptr_ = nullptr;

    return 0;
}
