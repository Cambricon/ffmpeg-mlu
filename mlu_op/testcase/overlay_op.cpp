/*************************************************************************
 * Copyright (C) [2024] by Cambricon, Inc. All rights reserved
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

#include "mluop_context.hpp"

int process_overlay(param_ctx_t ctx);

void overlay_op(params_conf &op_conf) {
    param_ctx_t ctx;
    ctx.input_file_bg = op_conf.find("input_file_bg")->second;
    ctx.input_file_fg = op_conf.find("input_file_fg")->second;
    ctx.output_file   = op_conf.find("output_file")->second;
    ctx.src_bg_pix_fmt= op_conf.find("src_bg_pixfmt")->second;
    ctx.src_fg_pix_fmt= op_conf.find("src_fg_pixfmt")->second;
    ctx.blend_pix_fmt = op_conf.find("blend_pixfmt")->second;
    ctx.src_bg_w      = std::atoi(op_conf.find("src_bg_w")->second.c_str());
    ctx.src_bg_h      = std::atoi(op_conf.find("src_bg_h")->second.c_str());
    ctx.src_fg_w      = std::atoi(op_conf.find("src_fg_w")->second.c_str());
    ctx.src_fg_h      = std::atoi(op_conf.find("src_fg_h")->second.c_str());
    ctx.src_roi_x     = std::atoi(op_conf.find("blend_roi_x")->second.c_str());
    ctx.src_roi_y     = std::atoi(op_conf.find("blend_roi_y")->second.c_str());
    ctx.src_roi_w     = std::atoi(op_conf.find("blend_roi_w")->second.c_str());
    ctx.src_roi_h     = std::atoi(op_conf.find("blend_roi_h")->second.c_str());
    ctx.frame_num     = std::atoi(op_conf.find("frame_num")->second.c_str());
    ctx.thread_num    = std::atoi(op_conf.find("thread_num")->second.c_str());
    ctx.device_id     = std::atoi(op_conf.find("device_id")->second.c_str());

    process_overlay(ctx);
}

int process_overlay(param_ctx_t ctx) {
    uint32_t width_bg  = ctx.src_bg_w;
    uint32_t height_bg = ctx.src_bg_h;
    uint32_t width_fg  = ctx.src_fg_w;
    uint32_t height_fg = ctx.src_fg_h;
    uint32_t frame_num = ctx.frame_num;
    uint32_t device_id = ctx.device_id;
    std::string filename_bg = ctx.input_file_bg;
    std::string filename_fg = ctx.input_file_fg;
    std::string output_file = ctx.output_file;

    set_cnrt_ctx(device_id);

    uint32_t bg_stride[3],     fg_stride[3];
    uint32_t bg_plane_size[3], fg_plane_size[3];
    uint32_t bg_image_size[3], fg_image_size[3];

    uint32_t bg_size, fg_size;
    uint8_t  *bg_cpu, *fg_cpu, *dst_cpu;

    void *src_bg_ptr[4], *src_fg_ptr[4], *dst_ptr[4];
    void *src_bg_y_mlu = nullptr, *src_bg_uv_mlu = nullptr;
    void *src_fg_y_mlu = nullptr, *src_fg_uv_mlu = nullptr;
    void *src_bg_mlu   = nullptr, *src_fg_mlu    = nullptr;
    void *dst_y_mlu    = nullptr, *dst_uv_mlu    = nullptr;
    void *dst_mlu      = nullptr;

    cv::Mat bg_mat, dst_mat;
    bg_mat = cv::imread(filename_bg, cv::IMREAD_COLOR);

    printf("bg_pixfmt=%s, fg_pixfmt=%s, blend_pixfmt:%s\n",
        ctx.src_bg_pix_fmt.c_str(), ctx.src_fg_pix_fmt.c_str(), ctx.blend_pix_fmt.c_str());

    // prepair bg input image
    if (!strcmp(ctx.src_bg_pix_fmt.c_str(), "nv12") || !strcmp(ctx.src_bg_pix_fmt.c_str(), "nv21")) {
        cv::Mat bg_yuv_mat;
        bg_stride[0] = width_bg;
        bg_stride[1] = width_bg;
        bg_plane_size[0] = height_bg * bg_stride[0];
        bg_plane_size[1] = height_bg * bg_stride[1] * 1 / 2;

        bg_size = bg_plane_size[0] + bg_plane_size[1];
        bg_cpu  = (uint8_t *)malloc(bg_size);
        dst_cpu = (uint8_t *)malloc(bg_size);
        !strcmp(ctx.src_bg_pix_fmt.c_str(), "nv12") ?
            BGR24_TO_NV12(bg_mat, bg_yuv_mat) : BGR24_TO_NV21(bg_mat, bg_yuv_mat);
        for (uint32_t row = 0; row < height_bg; ++row) {
            memcpy(bg_cpu + row * bg_stride[0], bg_yuv_mat.ptr<uint8_t>(row),
                    bg_yuv_mat.cols * bg_yuv_mat.elemSize());
        }
        for (uint32_t row = 0; row < height_bg / 2; ++row) {
            memcpy(bg_cpu + bg_plane_size[0] + row * bg_stride[1],
                    bg_yuv_mat.ptr<uint8_t>(row + height_bg),
                    bg_yuv_mat.cols * bg_yuv_mat.elemSize());
        }

        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&src_bg_y_mlu),  bg_plane_size[0]));
        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&src_bg_uv_mlu), bg_plane_size[1]));
        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&dst_y_mlu),  bg_plane_size[0]));
        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&dst_uv_mlu), bg_plane_size[1]));
        MLUOP_TEST_CHECK(cnrtMemcpy(src_bg_y_mlu, bg_cpu, bg_plane_size[0], CNRT_MEM_TRANS_DIR_HOST2DEV));
        MLUOP_TEST_CHECK(cnrtMemcpy(src_bg_uv_mlu, (bg_cpu + bg_plane_size[0]), bg_plane_size[1], CNRT_MEM_TRANS_DIR_HOST2DEV));
        src_bg_ptr[0] = src_bg_y_mlu;
        src_bg_ptr[1] = src_bg_uv_mlu;
        dst_ptr[0]    = dst_y_mlu;
        dst_ptr[1]    = dst_uv_mlu;

    } else if (!strcmp(ctx.src_bg_pix_fmt.c_str(), "rgb24") ||
            !strcmp(ctx.src_bg_pix_fmt.c_str(), "bgr24")) {
        bg_stride[0] = width_bg * 3;
        bg_size = bg_stride[0] * height_bg;
        bg_cpu  = (uint8_t *)malloc(bg_size);
        dst_cpu = (uint8_t *)malloc(bg_size);
        memcpy(bg_cpu, bg_mat.data, bg_size);

        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&src_bg_mlu), bg_size));
        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&dst_mlu), bg_size));
        MLUOP_TEST_CHECK(cnrtMemcpy(src_bg_mlu, bg_cpu, bg_size, CNRT_MEM_TRANS_DIR_HOST2DEV));
        src_bg_ptr[0] = src_bg_mlu;
        dst_ptr[0]    = dst_mlu;
    } else if (!strcmp(ctx.src_bg_pix_fmt.c_str(), "rgba") ||
            !strcmp(ctx.src_bg_pix_fmt.c_str(), "bgra")) {
        bg_stride[0] = width_bg * 4;
        bg_size = bg_stride[0] * height_bg;
        bg_cpu  = (uint8_t *)malloc(bg_size);
        dst_cpu = (uint8_t *)malloc(bg_size);
        cv::Mat bg_mat_bgra(bg_mat.rows, bg_mat.cols, CV_8UC4);
        cv::cvtColor(bg_mat, bg_mat_bgra, cv::COLOR_BGR2BGRA);
        memcpy(bg_cpu, bg_mat_bgra.data, bg_size);

        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&src_bg_mlu), bg_size));
        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&dst_mlu), bg_size));
        MLUOP_TEST_CHECK(cnrtMemcpy(src_bg_mlu, bg_cpu, bg_size, CNRT_MEM_TRANS_DIR_HOST2DEV));
        src_bg_ptr[0] = src_bg_mlu;
        dst_ptr[0]    = dst_mlu;
    }

    cv::Mat fg_mat;
    fg_mat = cv::imread(filename_fg, cv::IMREAD_COLOR);
    if (!strcmp(ctx.src_fg_pix_fmt.c_str(), "nv12") || !strcmp(ctx.src_fg_pix_fmt.c_str(), "nv21")) {
        cv::Mat fg_yuv_mat;
        fg_stride[0] = width_fg;
        fg_stride[1] = width_fg;
        fg_plane_size[0] = height_fg * fg_stride[0];
        fg_plane_size[1] = height_fg * fg_stride[1] * 1 / 2;
        fg_size = fg_plane_size[0] + fg_plane_size[1];
        fg_cpu  = (uint8_t *)malloc(fg_size);
        !strcmp(ctx.src_fg_pix_fmt.c_str(), "nv12") ?
            BGR24_TO_NV12(fg_mat, fg_yuv_mat) : BGR24_TO_NV21(fg_mat, fg_yuv_mat);
        for (uint32_t row = 0; row < height_fg; ++row) {
            memcpy(fg_cpu + row * fg_stride[0], fg_yuv_mat.ptr<uint8_t>(row),
                    fg_yuv_mat.cols * fg_yuv_mat.elemSize());
        }
        for (uint32_t row = 0; row < height_fg / 2; ++row) {
            memcpy(fg_cpu + fg_plane_size[0] + row * fg_stride[1],
                    fg_yuv_mat.ptr<uint8_t>(row + height_fg),
                    fg_yuv_mat.cols * fg_yuv_mat.elemSize());
        }

        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&src_fg_y_mlu),  fg_plane_size[0]));
        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&src_fg_uv_mlu), fg_plane_size[1]));
        MLUOP_TEST_CHECK(cnrtMemcpy(src_fg_y_mlu, fg_cpu, fg_plane_size[0], CNRT_MEM_TRANS_DIR_HOST2DEV));
        MLUOP_TEST_CHECK(cnrtMemcpy(src_fg_uv_mlu, (fg_cpu + fg_plane_size[0]), fg_plane_size[1], CNRT_MEM_TRANS_DIR_HOST2DEV));
        src_fg_ptr[0] = src_fg_y_mlu;
        src_fg_ptr[1] = src_fg_uv_mlu;

    } else if (!strcmp(ctx.src_fg_pix_fmt.c_str(), "rgb24") ||
            !strcmp(ctx.src_fg_pix_fmt.c_str(), "bgr24")) {
        fg_stride[0] = width_fg * 3;
        fg_size = fg_stride[0] * height_fg;
        fg_cpu  = (uint8_t *)malloc(fg_size);
        memcpy(fg_cpu, fg_mat.data, fg_size);

        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&src_fg_mlu), fg_size));
        MLUOP_TEST_CHECK(cnrtMemcpy(src_fg_mlu, fg_cpu, fg_size, CNRT_MEM_TRANS_DIR_HOST2DEV));
        src_fg_ptr[0] = src_fg_mlu;
    } else if (!strcmp(ctx.src_fg_pix_fmt.c_str(), "rgba") ||
            !strcmp(ctx.src_fg_pix_fmt.c_str(), "bgra")) {
        fg_stride[0] = width_fg * 4;
        fg_size = fg_stride[0] * height_fg;
        fg_cpu  = (uint8_t *)malloc(fg_size);
        cv::Mat fg_mat_bgra(fg_mat.rows, fg_mat.cols, CV_8UC4);
        cv::cvtColor(fg_mat, fg_mat_bgra, cv::COLOR_BGR2BGRA);
        memcpy(fg_cpu, fg_mat_bgra.data, fg_size);

        MLUOP_TEST_CHECK(cnrtMalloc((void **)(&src_fg_mlu), fg_size));
        MLUOP_TEST_CHECK(cnrtMemcpy(src_fg_mlu, fg_cpu, fg_size, CNRT_MEM_TRANS_DIR_HOST2DEV));
        src_fg_ptr[0] = src_fg_mlu;
    }

    /*--------------------------------------------------------------------------------------------*/
    int ret;
    HANDLE handle;
    mluOpAPI mluop_api;
    std::shared_ptr<mluOpFuncList> op_funcs;
    op_funcs = mluop_api.getAPI();
    std::cout << "MLUOP_VERSION:" << op_funcs->mluOpGetVersion() << std::endl;

    double op_time = 0.0;
    timeWatch op_watcher;
    op_watcher.start();

    ret = op_funcs->mluOverlayInit(&handle,
            width_bg, height_bg,
            ctx.src_bg_pix_fmt.c_str(), // src bg pix_fmt
            ctx.src_fg_pix_fmt.c_str(), // src fg pix_fmt
            ctx.blend_pix_fmt.c_str()); // alphablend pix_fmt
    if (ret) {
        throw std::runtime_error("mluOverlay init failed");
    }
    op_time = op_watcher.stop();
    std::cout << "init time:" << op_time << "ms" << std::endl;
    op_time = 0.0;
    float alpha = 0.5, beta = 0.5, gamma = 0.0;

    for (uint32_t i = 0; i < frame_num; i++) {
        op_watcher.start();
        ret = op_funcs->mluOverlayExec(handle,
                dst_ptr, src_bg_ptr, src_fg_ptr,
                width_fg,       height_fg,
                ctx.src_roi_x, ctx.src_roi_y,
                ctx.src_roi_w, ctx.src_roi_h,
                alpha, beta, gamma);
        if (ret) {
            throw std::runtime_error("mluOverlay exec failed");
        }
        op_time += op_watcher.stop();
    }
    std::cout << "exec time(ave.):" << op_time / frame_num
              << "ms, total frames:" << frame_num << std::endl;

    cv::Mat dst_img;
    if (!strcmp(ctx.src_bg_pix_fmt.c_str(), "nv12") ||
            !strcmp(ctx.src_bg_pix_fmt.c_str(), "nv21")) {
        MLUOP_TEST_CHECK(cnrtMemcpy(dst_cpu, dst_ptr[0], bg_plane_size[0], CNRT_MEM_TRANS_DIR_DEV2HOST));
        MLUOP_TEST_CHECK(cnrtMemcpy(dst_cpu + bg_plane_size[0], dst_ptr[1] , bg_plane_size[1], CNRT_MEM_TRANS_DIR_DEV2HOST));
        dst_img = cv::Mat(height_bg * 3 / 2, width_bg, CV_8UC1);
        memcpy(dst_img.data, dst_cpu, height_bg * width_bg * 3 / 2);
        !strcmp(ctx.src_bg_pix_fmt.c_str(), "nv12") ?
                cv::cvtColor(dst_img, dst_img, cv::COLOR_YUV2BGRA_NV12):
                cv::cvtColor(dst_img, dst_img, cv::COLOR_YUV2BGRA_NV21);
        cv::imwrite(output_file, dst_img);
    } else if (!strcmp(ctx.src_bg_pix_fmt.c_str(), "rgb24") ||
            !strcmp(ctx.src_bg_pix_fmt.c_str(), "bgr24")) {
        MLUOP_TEST_CHECK(cnrtMemcpy(dst_cpu, dst_ptr[0], bg_size, CNRT_MEM_TRANS_DIR_DEV2HOST));
        dst_img = cv::Mat(height_bg, width_bg, CV_8UC3);
        memcpy(dst_img.data, dst_cpu, height_bg * width_bg * 3);
        cv::imwrite(output_file, dst_img);
    } else if (!strcmp(ctx.src_bg_pix_fmt.c_str(), "rgba") ||
            !strcmp(ctx.src_bg_pix_fmt.c_str(), "bgra")) {
        MLUOP_TEST_CHECK(cnrtMemcpy(dst_cpu, dst_ptr[0], bg_size, CNRT_MEM_TRANS_DIR_DEV2HOST));
        dst_img = cv::Mat(height_bg, width_bg, CV_8UC4);
        memcpy(dst_img.data, dst_cpu, height_bg * width_bg * 4);
        cv::imwrite(output_file, dst_img);
    }

    op_time = 0.0;
    op_watcher.start();
    ret = op_funcs->mluOverlayDestroy(handle);
    if (ret) {
        throw std::runtime_error("mluOverlay destroy failed");
    }
    op_time = op_watcher.stop();
    std::cout << "destroy time:" << op_time << "ms" << std::endl;

    if (bg_cpu)        free(bg_cpu);
    if (fg_cpu)        free(fg_cpu);
    if (dst_cpu)       free(dst_cpu);
    if (src_bg_y_mlu)  cnrtFree(src_bg_y_mlu);
    if (src_fg_y_mlu)  cnrtFree(src_fg_y_mlu);
    if (src_bg_uv_mlu) cnrtFree(src_bg_uv_mlu);
    if (src_fg_uv_mlu) cnrtFree(src_fg_uv_mlu);
    if (dst_y_mlu)     cnrtFree(dst_y_mlu);
    if (dst_uv_mlu)    cnrtFree(dst_uv_mlu);
    if (src_bg_mlu)    cnrtFree(src_bg_mlu);
    if (src_fg_mlu)    cnrtFree(src_fg_mlu);
    if (dst_mlu)       cnrtFree(dst_mlu);

    return 0;
}
