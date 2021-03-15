#!/bin/bash
source env.sh
#debug
#cncc -fPIC --bang-mlu-arch=MLU270 -O3 -DNDEBUG -I. -c resize_rgb.mlu -o resize_rgbx.o
#cncc -fPIC --bang-mlu-arch=MLU270 -O3 -DNDEBUG -I. -c resize_rgb_block.mlu -o resize_rgbx_block.o

## method 1
#cncc -fPIC --bang-mlu-arch=MLU270 -O3 -I. -c resize_rgb.mlu -o resize_rgbx.o
#cncc -fPIC --bang-mlu-arch=MLU270 -O3 -I. -c resize_rgb_block.mlu -o resize_rgbx_block.o
#g++ -std=c++11 -I/usr/local/neuware/include resize_rgb.cc resize_rgbx_block.o resize_rgbx.o -L/usr/local/neuware/lib64 -lcnrt -lcndrv

# ## method 2
# cncc -fPIC --bang-mlu-arch=MLU270 -O3 -I. -c resize_rgb.mlu -o resize_rgbx.o
# cncc -fPIC --bang-mlu-arch=MLU270 -O3 -I. -c resize_rgb_block.mlu -o resize_rgbx_block.o
# ld -r resize_rgbx_block.o resize_rgbx.o -o resize_rgbx_x86.o
# g++ -std=c++11 -I/usr/local/neuware/include resize_rgb.cc resize_rgbx_x86.o -L/usr/local/neuware/lib64 -lcnrt -lcndrv

## method 3
#cncc -fPIC --bang-mlu-arch=MLU270 -O3 -I. -c resize_rgb.mlu -o resize_rgbx.o
#cncc -fPIC --bang-mlu-arch=MLU270 -O3 -I. -c resize_rgb_block.mlu -o resize_rgbx_block.o
#ar cvr libresize_rgbx_x86.a resize_rgbx_block.o resize_rgbx.o
#g++ -std=c++11 -I/usr/local/neuware/include resize_rgb.cc libresize_rgbx_x86.a -L/usr/local/neuware/lib64 -lcnrt -lcndrv

cncc resize_rgb_block.mlu -O3 -o resize_rgbx_x86.o --bang-mlu-arch=MLU270 --bang-device-only --target=x86_64-linux-gnu
g++ -std=c++11 -I/usr/local/neuware/include resize_rgb.cc resize_rgbx_x86.o -L/usr/local/neuware/lib64 -lcnrt -lcndrv