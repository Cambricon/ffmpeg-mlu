#!/bin/bash
#--------------------------------
CUR_DIR=$(dirname $(readlink -f "${BASH_SOURCE[0]}") )

echo "Info: clean nasm"
pushd ${CUR_DIR}/nasm
    make distclean
popd
rm -rf ${CUR_DIR}/nasm

echo "Info: clean yasm"
pushd ${CUR_DIR}/yasm
    make distclean
popd
rm -rf ${CUR_DIR}/yasm

echo "Info: clean aac"
pushd ${CUR_DIR}/fdk-aac
    make distclean
popd
rm -rf ${CUR_DIR}/fdk-aac

echo "Info: clean lame"
pushd ${CUR_DIR}/lame
    make distclean
popd
rm -rf ${CUR_DIR}/lame

echo "Info: clean opus"
pushd ${CUR_DIR}/opus
    make distclean
popd
rm -rf ${CUR_DIR}/opus

echo "Info: clean x264"
pushd ${CUR_DIR}/x264
    make distclean
popd
rm -rf ${CUR_DIR}/x264

echo "Info: clean x265"
pushd ${CUR_DIR}/x265/build/linux
    make distclean && make clean && rm -rf x265* cmake* encoder common CM* Makefile* install*
popd
rm -rf ${CUR_DIR}/x265

echo "Info: clean davs2"
pushd ${CUR_DIR}/davs2/build/linux
    make distclean && make clean && rm -rf common
popd
rm -rf ${CUR_DIR}/davs2

echo "Info: clean xavs2"
pushd ${CUR_DIR}/xavs2/build/linux
    make distclean && make clean && rm -rf common config.h config.log config.mak encoder test xavs2*
popd
rm -rf ${CUR_DIR}/xavs2

echo "clean all 3rdparty library done"
