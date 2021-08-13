#!/bin/bash
#--------------------------------

DIR=${PWD}

echo ""
echo "--------------clean nasm----------------"
cd ${DIR}/nasm && make distclean && cd ..

echo ""
echo "--------------clean yasm----------------"
cd ${DIR}/yasm && make distclean && cd ../

echo ""
echo "---------------clean aac---------------"
cd ${DIR}/fdk-aac && make distclean && cd ../

echo ""
echo "---------------clean lame--------------"
cd ${DIR}/lame && make distclean && cd ../

echo ""
echo "---------------clean opus--------------"
cd ${DIR}/opus && make distclean && cd ../
cd ${DIR}

echo ""
echo "--------------clean x264---------------"
cd ${DIR}/x264 && make distclean && cd ../

echo ""
echo "--------------clean x265---------------"
cd ${DIR}/x265/build/linux && make distclean && make clean \
    && rm -rf x265* cmake* encoder common CM* Makefile* install*

echo ""
