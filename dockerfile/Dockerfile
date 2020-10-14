#/*************************************************************************
# * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
# *
# *  Licensed under the Apache License, Version 2.0 (the "License");
# *  you may not use this file except in compliance with the License.
# *  You may obtain a copy of the License at
# *
# *     http://www.apache.org/licenses/LICENSE-2.0
# *
# * The above copyright notice and this permission notice shall be included in
# * all copies or substantial portions of the Software.
# * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# * THE SOFTWARE.
# *************************************************************************/

# 1. copy your neuware package into the directory of ffmpeg-mlu-rel
# 2. docker build -f dockerfile/Dockerfile --build-arg mlu_platform=MLU270 --build-arg neuware_package=neuware-mlu270-1.4.0-1_Ubuntu16.04_amd64.deb -t ubuntu_ffmpeg-mlu:v1 .
# 3. docker run -v /tmp/.X11-unix:/tmp/.X11-unix -e DISPLAY=$DISPLAY --privileged -v /dev:/dev --net=host --pid=host --ipc=host -v $HOME/.Xauthority -it --name ubuntu_ffmpeg-mlu  -v $PWD:/workspace ubuntu_ffmpeg-mlu:v1
# 4. if docker is ready, can execute it by docker start ubuntu_ffmpeg-mlu / docker exec -it  ubuntu_ffmpeg-mlu /bin/bash

FROM ubuntu:16.04

MAINTAINER <Cambricon, Inc.>

WORKDIR /root/ffmpeg-mlu/

ARG neuware_version=neuware-mlu270-1.4.0
ARG neuware_package=${neuware_version}-1_Ubuntu16.04_amd64.deb
ARG mlu_platform=MLU270

RUN echo -e 'nameserver 114.114.114.114' > /etc/resolv.conf

RUN echo deb http://mirrors.aliyun.com/ubuntu/ xenial main restricted  > /etc/apt/sources.list && \
    echo deb-src http://mirrors.aliyun.com/ubuntu/ xenial main restricted multiverse universe >> /etc/apt/sources.list && \
    echo deb http://mirrors.aliyun.com/ubuntu/ xenial-updates main restricted >> /etc/apt/sources.list && \
    echo deb http://mirrors.aliyun.com/ubuntu/ xenial universe >> /etc/apt/sources.list && \
    echo deb http://mirrors.aliyun.com/ubuntu/ xenial-updates universe >> /etc/apt/sources.list && \
    echo deb http://mirrors.aliyun.com/ubuntu/ xenial multiverse >> /etc/apt/sources.list && \
    echo deb http://mirrors.aliyun.com/ubuntu/ xenial-updates multiverse >> /etc/apt/sources.list && \
    echo deb http://mirrors.aliyun.com/ubuntu/ xenial-backports main restricted universe multiverse >> /etc/apt/sources.list && \
    echo deb http://mirrors.aliyun.com/ubuntu/ xenial-security main restricted >> /etc/apt/sources.list && \
    echo deb http://mirrors.aliyun.com/ubuntu/ xenial-security universe  >> /etc/apt/sources.list && \
    echo deb http://mirrors.aliyun.com/ubuntu/ xenial-security multiverse >> /etc/apt/sources.list && \
    apt-get update && apt-get upgrade -y && apt-get install -y --no-install-recommends \
            curl git wget vim build-essential make ca-certificates nasm yasm \
            openssh-server \
            net-tools && \
    apt-get clean && \
    apt-get update --fix-missing && \
    rm -rf /var/lib/apt/lists/*

COPY . /root/ffmpeg-mlu/

RUN dpkg -i /root/ffmpeg-mlu/$neuware_package && \
    dpkg -i /var/${neuware_version}/cndev*.deb && \
    dpkg -i /var/${neuware_version}/cndrv*.deb && \
    dpkg -i /var/${neuware_version}/cnrt*.deb && \
    dpkg -i /var/${neuware_version}/cncodec*.deb && \
    git clone https://gitee.com/mirrors/ffmpeg.git -b release/4.2 --depth=1 && \
    cd ffmpeg && \
    git apply ../ffmpeg4.2_mlu.patch && \
    mkdir build && cd build && \
    ../configure --enable-gpl --enable-version3 --enable-mlumpp --extra-cflags="-I/usr/local/neuware/include" --extra-ldflags="-L/usr/local/neuware/lib64" --extra-libs="-lcncodec -lcnrt -ldl -lcndrv" && \
    make -j && \
    rm -rf /root/ffmpeg-mlu/$neuware_package

WORKDIR /root/ffmpeg-mlu/ffmpeg/build

