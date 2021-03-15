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
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <string>
#include <vector>
#include "mluop.h"
#include "cnrt.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

typedef void *HANDLE;
#define save_flag 0

struct MLUSRContext {
 public:
  int input_width;                    //网络输入的宽度
  int input_height;                   //网络输入的高度
  int batch_size;                     //网络的batch
  int input_channel;                  //网络输入的通道数

  int output_width;                   //网络输出的宽度
  int output_height;                  //网络输出的高度
  int output_channel;                 //网络输出的通道数
  int64_t *inputSizeS;                //网络输入数据大小,用于分配内存
  int64_t *outputSizeS;               //网络输出数据量大小,用于分配内存

  cnrtDataType_t *inputTypeS;         //网络输入的数据类型
  cnrtDataType_t *outputTypeS;        //网络输出的数据类型
  std::vector<int> output_count;

  cnrtQueue_t queue;                  //cnrt queue
  cnrtModel_t model;                  //离线模型
  cnrtFunction_t function;            //离线模型中的Function
  cnrtDev_t dev;                      //MLU设备句柄
  cnrtRuntimeContext_t ctx;           //推理上下文
  int inputNum;                       //输入节点个数
  int outputNum;                      //输出节点个数

  void **param;                       //保存推理时,mlu上内存地址的指针
  void **cpuTempData;                 //输入数据的CPU temp

  void **cpuTrans_;
  void **firstConvData_;
  void **cpuData_;

  void **inputCpuPtrS;                //输入数据的CPU指针
  void **outputCpuPtrS;               //输出数据的CPU指针
  void **outputCpuNchwPtrS;
  void **inputMluPtrS;                //输入数据的MLU指针
  void **outputMluPtrS;               //输出数据的MLU指针
  cnrtNotifier_t notifier_start;      //用来记录硬件时间
  cnrtNotifier_t notifier_end;
  cnrtInvokeParam_t invokeParam;      //invoke参数

  cnrtDataType_t* input_data_type;
  cnrtDataType_t* output_data_type;

  unsigned int affinity;              //mlu cluster param
  std::vector<std::vector<cv::Mat> > preprocessedImages;
};

double GetTickCount() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000000 + ts.tv_nsec / 1000)/1000.0;
}

extern "C" int mluop_infer_sr_init(HANDLE* handle, const char* model_path,
                                   int dev_id, int dev_channel) {
  MLUSRContext *sr_ctx_ = new MLUSRContext;
  *handle = static_cast<void *>(sr_ctx_);
  unsigned int dev_num=0;
  CNRT_CHECK(cnrtInit(0));
  CNRT_CHECK(cnrtGetDeviceCount(&dev_num));
  if (dev_num == 0) return -1;
  int *dimValues;					//保存维度shape
  int dimNum;						  //保存维度大小

  //获取指定设备的句柄
  CNRT_CHECK(cnrtGetDeviceHandle(&sr_ctx_->dev, dev_id));
  //设置当前使用的设备,作用于线程上下文
  CNRT_CHECK(cnrtSetCurrentDevice(sr_ctx_->dev));
  //加载离线模型
  CNRT_CHECK(cnrtLoadModel(&sr_ctx_->model, model_path));
  //创建function
  CNRT_CHECK(cnrtCreateFunction(&sr_ctx_->function));
  //从离线模型中提取指定的function,离线模型可以存储多个function
  CNRT_CHECK(cnrtExtractFunction(&sr_ctx_->function, sr_ctx_->model, "subnet0"));
  //创建运行时
  CNRT_CHECK(cnrtCreateRuntimeContext(&sr_ctx_->ctx,sr_ctx_->function,NULL));
  //设置运行时使用的设备ID
  CNRT_CHECK(cnrtSetRuntimeContextDeviceId(sr_ctx_->ctx,dev_id));
  //调用cnrtSetCurrentChannel之后 CNRT 仅在指定的通道上分配MLU内存,否则采用交织的方式分配
  if(dev_channel>=0) {
    CNRT_CHECK(cnrtSetCurrentChannel((cnrtChannelType_t)dev_channel));
    CNRT_CHECK(cnrtSetRuntimeContextChannel(sr_ctx_->ctx,(cnrtChannelType_t)(dev_channel)));
  }
  //初始化运行时
  CNRT_CHECK(cnrtInitRuntimeContext(sr_ctx_->ctx, NULL));
  //创建队列
  CNRT_CHECK(cnrtRuntimeContextCreateQueue(sr_ctx_->ctx, &sr_ctx_->queue));

  //获取模型输入/输出 的数据大小及节点个数
  CNRT_CHECK(cnrtGetInputDataSize(&sr_ctx_->inputSizeS, &sr_ctx_->inputNum, sr_ctx_->function));
  CNRT_CHECK(cnrtGetOutputDataSize(&sr_ctx_->outputSizeS, &sr_ctx_->outputNum, sr_ctx_->function));

  //获取模型输入/输出 的数据类型
  CNRT_CHECK(cnrtGetInputDataType(&sr_ctx_->inputTypeS, &sr_ctx_->inputNum, sr_ctx_->function));
  CNRT_CHECK(cnrtGetOutputDataType(&sr_ctx_->outputTypeS, &sr_ctx_->outputNum, sr_ctx_->function));

  //分配 存放CPU端输入/输出地址的 指针数组
  sr_ctx_->inputCpuPtrS = (void **)malloc(sizeof(void *) * sr_ctx_->inputNum);
  sr_ctx_->cpuTempData = (void **)malloc(sizeof(void *) * sr_ctx_->inputNum);

  sr_ctx_->outputCpuPtrS = (void **)malloc(sizeof(void *) * sr_ctx_->outputNum);
  sr_ctx_->outputCpuNchwPtrS = (void **)malloc(sizeof(void *) * sr_ctx_->outputNum);

  //分配 存放MLU端输入/输出地址的 指针数组
  sr_ctx_->outputMluPtrS = (void **)malloc(sizeof(void *) * sr_ctx_->outputNum);
  sr_ctx_->inputMluPtrS = (void **)malloc(sizeof(void *) * sr_ctx_->inputNum);

  //获取模型输入/输出 的数据大小及节点个数
  //为输入节点 分配CPU/MLU内存
  for (int i = 0; i < sr_ctx_->inputNum; i++) {
    CNRT_CHECK(cnrtMalloc(&sr_ctx_->inputMluPtrS[i], sr_ctx_->inputSizeS[i]));	  //分配MLU上内存
    //获取输入的维度信息 NHWC
    CNRT_CHECK(cnrtGetInputDataShape(&dimValues, &dimNum, i, sr_ctx_->function));
    if( i == 0 ) {
      sr_ctx_->batch_size=dimValues[0];
      sr_ctx_->input_channel=dimValues[3] - 1;
      sr_ctx_->input_height=dimValues[1];
      sr_ctx_->input_width=dimValues[2];
    }
    sr_ctx_->inputCpuPtrS[i] = (void *)malloc(sr_ctx_->inputSizeS[i]);   //分配CPU上的内存
    sr_ctx_->cpuTempData[i] = (void *)malloc(sr_ctx_->inputSizeS[i]);    //分配CPU上的内存
    printf("input shape:\n");
    for(int y=0;y<dimNum;y++) {
      if(y == 3) printf("%d ",dimValues[y]-1);
      else printf("%d ",dimValues[y]);
    }
    printf("\n");
    free(dimValues);
  }

  //为输出节点 分配CPU/MLU内存
  for (int i = 0; i < sr_ctx_->outputNum; i++) {
    CNRT_CHECK(cnrtMalloc(&sr_ctx_->outputMluPtrS[i], sr_ctx_->outputSizeS[i]));  //分配MLU上内存
    sr_ctx_->outputCpuPtrS[i] = (void *)malloc(sr_ctx_->outputSizeS[i]);         //分配CPU上的内存

    //获取输出的维度信息 NHWC
    CNRT_CHECK(cnrtGetOutputDataShape(&dimValues,&dimNum, i, sr_ctx_->function));
    if( i == 0 ) {
      sr_ctx_->batch_size=dimValues[0];
      sr_ctx_->output_channel=dimValues[3];
      sr_ctx_->output_height=dimValues[1];
      sr_ctx_->output_width=dimValues[2];
    }
    int count=1;
    printf("output shape:\n");
    for(int y=0;y<dimNum;y++) {
      printf("%d ",dimValues[y]);
      count=count*dimValues[y];
    }
    printf("\n");
    sr_ctx_->outputCpuNchwPtrS[i] = (void *)malloc(count*sizeof(float)); //将输出转为float32类型,方便用户后处理
    sr_ctx_->output_count.push_back(count);
    free(dimValues);
  }

  //创建事件
  CNRT_CHECK(cnrtRuntimeContextCreateNotifier(sr_ctx_->ctx,&sr_ctx_->notifier_start));
  CNRT_CHECK(cnrtRuntimeContextCreateNotifier(sr_ctx_->ctx,&sr_ctx_->notifier_end));

  //配置MLU输入/输出 地址的指针
  sr_ctx_->param = (void **)malloc(sizeof(void *) * (sr_ctx_->inputNum + sr_ctx_->outputNum));
  for (int i = 0; i < sr_ctx_->inputNum; i++) {
      sr_ctx_->param[i] = sr_ctx_->inputMluPtrS[i];
  }
  for (int i = 0; i < sr_ctx_->outputNum; i++) {
      sr_ctx_->param[i + sr_ctx_->inputNum] = sr_ctx_->outputMluPtrS[i];
  }

  //设置invoke的参数
  sr_ctx_->affinity = 1<<dev_channel;    //设置通道亲和性,使用指定的MLU cluster做推理
  sr_ctx_->invokeParam.invoke_param_type = CNRT_INVOKE_PARAM_TYPE_0;
  sr_ctx_->invokeParam.cluster_affinity.affinity = &sr_ctx_->affinity;

  //设置输入/输出的节点 索引
  int input_count =sr_ctx_->batch_size*sr_ctx_->input_channel*sr_ctx_->input_height*sr_ctx_->input_width;

  sr_ctx_->cpuData_ = new(void*);
  sr_ctx_->cpuData_[0] = new float[input_count];
  sr_ctx_->cpuTrans_ = new(void*);
  sr_ctx_->cpuTrans_[0] = new float[input_count];
  sr_ctx_->firstConvData_ = new(void*);
  sr_ctx_->firstConvData_[0] = new char[input_count];

  CNRT_CHECK(cnrtGetInputDataType(&sr_ctx_->input_data_type, &sr_ctx_->inputNum, sr_ctx_->function));
  CNRT_CHECK(cnrtGetOutputDataType(&sr_ctx_->output_data_type, &sr_ctx_->outputNum, sr_ctx_->function));

  void* inputData =  reinterpret_cast<float*>(sr_ctx_->cpuData_[0]);
  for(int i = 0 ; i < sr_ctx_->batch_size ; i++) {
    sr_ctx_->preprocessedImages.push_back(std::vector<cv::Mat> ());
    for(int j = 0 ; j < 3 ; j++) {
      cv::Mat channel(sr_ctx_->input_height, sr_ctx_->input_width, CV_32FC1, inputData);
      sr_ctx_->preprocessedImages[i].push_back(channel);
      inputData += (sr_ctx_->input_height*sr_ctx_->input_width*sizeof(float));
    }
  }
  return 0;
}

extern "C" int mluop_infer_sr_exec(HANDLE handle, void *data_in, uint32_t in_linesize,
                                   uint32_t out_linesize, void *data_out) {
  MLUSRContext *sr_ctx_ = (MLUSRContext *)handle;
  void* firstConvTempData = reinterpret_cast<void*>(sr_ctx_->firstConvData_[0]);
  for(int i = 0 ; i < sr_ctx_->batch_size ; i++) {
    cv::Mat input_image=cv::Mat(sr_ctx_->input_height, in_linesize / 3, CV_8UC3, data_in);
    // cv::Mat input_image_resized;
    // cv::resize(input_image,input_image_resized,cv::Size(sr_ctx_->input_width, sr_ctx_->input_height),0, 0, cv::INTER_LINEAR);
    cv::Mat sample_rgb;
    if (sr_ctx_->input_width == ((int)(in_linesize / 3))) {
      cv::cvtColor(input_image, sample_rgb, cv::COLOR_BGR2RGB);
    } else {
      cv::Mat input_image_crop = input_image(cv::Rect(0, 0, sr_ctx_->input_width, sr_ctx_->input_height)).clone();
      cv::cvtColor(input_image_crop, sample_rgb, cv::COLOR_BGR2RGB);
    }
    cv::Mat net_input_data_rgb(sr_ctx_->input_height,sr_ctx_->input_width,CV_32FC3);
    sample_rgb.convertTo(net_input_data_rgb, CV_32FC3);

    cv::split(net_input_data_rgb, sr_ctx_->preprocessedImages[i]);

    void* temp_ptr = nullptr;
    int dim_shape[4] = {sr_ctx_->inputNum, sr_ctx_->input_channel, sr_ctx_->input_height, sr_ctx_->input_width};
    int dim_order[4] = {0, 2, 3, 1};
    cnrtTransDataOrder(sr_ctx_->cpuData_[i],
                        CNRT_FLOAT32,
                        sr_ctx_->cpuTrans_[i],
                        4,
                        dim_shape,
                        dim_order);
    int temp_input_count = sr_ctx_->batch_size * sr_ctx_->input_channel * sr_ctx_->input_height * sr_ctx_->input_width;

    cnrtCastDataType(sr_ctx_->cpuTrans_[i],
                      CNRT_FLOAT32,
                      (sr_ctx_->input_data_type[i] == CNRT_UINT8) ? firstConvTempData : sr_ctx_->cpuTempData[i],
                      sr_ctx_->input_data_type[i],
                      temp_input_count,
                      nullptr);

    if (sr_ctx_->input_data_type[i] == CNRT_UINT8) {
      int inputDimValue[4] = {sr_ctx_->inputNum, sr_ctx_->input_height, sr_ctx_->input_width, sr_ctx_->input_channel};
      int inputDimStride[4] = {0, 0, 0, 1};
      cnrtAddDataStride(firstConvTempData, CNRT_UINT8, sr_ctx_->cpuTempData[i], 4,
                          inputDimValue, inputDimStride);
    }
    temp_ptr = sr_ctx_->cpuTempData[i];
    //设置输入/输出的节点 索引
    int input_idx=0;
    CNRT_CHECK(cnrtMemcpy(sr_ctx_->inputMluPtrS[input_idx],temp_ptr,sr_ctx_->inputSizeS[input_idx],CNRT_MEM_TRANS_DIR_HOST2DEV));
  }
  // 拷贝输入数据到MLU内存
  // auto t0 = GetTickCount();
  CNRT_CHECK(cnrtPlaceNotifier(sr_ctx_->notifier_start, sr_ctx_->queue));
  CNRT_CHECK(cnrtInvokeRuntimeContext(sr_ctx_->ctx, sr_ctx_->param, sr_ctx_->queue, &sr_ctx_->invokeParam));
  CNRT_CHECK(cnrtPlaceNotifier(sr_ctx_->notifier_end, sr_ctx_->queue));
  CNRT_CHECK(cnrtSyncQueue(sr_ctx_->queue));
  // auto t1 = GetTickCount();

  float hwtime;
  CNRT_CHECK(cnrtNotifierDuration(sr_ctx_->notifier_start, sr_ctx_->notifier_end, &hwtime));
  //printf("HardwareTime:%f(ms) E2ETime:%f(ms)\n", hwtime/1000.0, t1 - t0);

  //拷贝MLU输出到CPU内存
  for (int i = 0; i < sr_ctx_->outputNum; i++) {
    CNRT_CHECK(cnrtMemcpy(sr_ctx_->outputCpuPtrS[i], sr_ctx_->outputMluPtrS[i], sr_ctx_->outputSizeS[i],CNRT_MEM_TRANS_DIR_DEV2HOST));
    //数据类型转换 half->float32
    CNRT_CHECK(cnrtCastDataType(reinterpret_cast<void*>(sr_ctx_->outputCpuPtrS[i]), sr_ctx_->outputTypeS[i],
                    reinterpret_cast<void*>(sr_ctx_->outputCpuNchwPtrS[i]),
                    CNRT_FLOAT32, sr_ctx_->output_width*sr_ctx_->output_height*sr_ctx_->output_channel,nullptr));
  }

  int cout = sr_ctx_->output_channel * sr_ctx_->output_height * sr_ctx_->output_width;
  // 输出结果
  for(int i=0; i < sr_ctx_->outputNum; i++) {
    float *batch_data=(float*)sr_ctx_->outputCpuNchwPtrS[i];
    for (int b = 0; b < sr_ctx_->batch_size; b++) {
      cv::Mat tmp = cv::Mat(sr_ctx_->output_height, sr_ctx_->output_width, CV_32FC3, (void*)batch_data);

      cv::Mat out, out_8u;
      cv::add(tmp, cv::Scalar(114.444, 111.4605, 103.02), out);
      out.convertTo(out_8u, CV_8UC3);
      cv::Mat output = cv::Mat(sr_ctx_->output_height, out_linesize / 3, CV_8UC3, data_out);
      if (sr_ctx_->output_width == ((int)(out_linesize / 3))) {
        cv::cvtColor(out_8u, output, CV_BGR2RGB);
      } else {
        cv::Mat RGB;
        cv::cvtColor(out_8u, RGB, CV_BGR2RGB);
        RGB.copyTo(output(cv::Rect(0, 0, sr_ctx_->output_width, sr_ctx_->output_height)));
      }
      if (save_flag) {
         float* data = (float*)malloc(sizeof(float) * cout);
         for (int c=0; c<cout; c++){
           data[c] = batch_data[c+b*cout];
         }
         std::string outFile = "output_rcan_1280_720.png";
         std::stringstream ss;
         ss << "output_rcan_1280_720_" << i << ".png";
         ss >> outFile;
         cv::imwrite(outFile.c_str(), output);
         free(data);
       }
    }
  }
  return 0;
}

extern "C" int mluop_infer_sr_destroy(void* handle) {
  MLUSRContext *sr_ctx_ = (MLUSRContext *)handle;
  CNRT_CHECK(cnrtSetCurrentDevice(sr_ctx_->dev));
  CNRT_CHECK(cnrtDestroyQueue(sr_ctx_->queue));
  CNRT_CHECK(cnrtDestroyFunction(sr_ctx_->function));
  CNRT_CHECK(cnrtUnloadModel(sr_ctx_->model));

  cnrtDestroyNotifier(&sr_ctx_->notifier_start);
  cnrtDestroyNotifier(&sr_ctx_->notifier_end);

  for (int i = 0; i < sr_ctx_->inputNum; i++) {
    free(sr_ctx_->inputCpuPtrS[i]);
    free(sr_ctx_->cpuTempData[i]);
    cnrtFree(sr_ctx_->inputMluPtrS[i]);
  }
   for (int i = 0; i < sr_ctx_->outputNum; i++) {
     free(sr_ctx_->outputCpuPtrS[i]);
     free(sr_ctx_->outputCpuNchwPtrS[i]);
     cnrtFree(sr_ctx_->outputMluPtrS[i]);
   }

   free(sr_ctx_->param);
   free(sr_ctx_->inputCpuPtrS);
   free(sr_ctx_->cpuTempData);
   free(sr_ctx_->outputCpuPtrS);
  cnrtDestroyRuntimeContext(sr_ctx_->ctx);

  return 0;
}
