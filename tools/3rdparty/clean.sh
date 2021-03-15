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
echo "--------------clean x264---------------"
cd ${DIR}/x264_2016 && make distclean && cd ../
#cd ${DIR}/x264 && make distclean && cd ../

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
