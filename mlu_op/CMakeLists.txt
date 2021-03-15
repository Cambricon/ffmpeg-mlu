cmake_minimum_required(VERSION 2.8)

project(EasyOP C CXX)

set(TARGET easyOP)

option(RELEASE "build type" ON)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
  message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

# -- Build Flags
if(RELEASE)
  set(CMAKE_BUILD_TYPE Release)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall")
  set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O2 -s")
else()
  set(CMAKE_BUILD_TYPE Debug)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O3 -fPIC")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O3 -fPIC")
endif()

if(NOT DEFINED ENV{NEUWARE_HOME})
  set(ENV{NEUWARE_HOME} /usr/local/neuware)
endif()

find_package( OpenCV REQUIRED )

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib/)

link_directories("$ENV{NEUWARE_HOME}/lib64")
link_directories(${PROJECT_SOURCE_DIR}/lib)
include_directories($ENV{NEUWARE_HOME}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SYSTEM_LIBS cnrt)

file(GLOB_RECURSE sources_1 ${CMAKE_CURRENT_SOURCE_DIR}/src/yuv2yuv_invoke_resize.cpp)
file(GLOB_RECURSE sources_2 ${CMAKE_CURRENT_SOURCE_DIR}/obj/resizeYuvKernel_x86.o)
file(GLOB_RECURSE sources_3 ${CMAKE_CURRENT_SOURCE_DIR}/src/rgbx2rgbx_invoke_resize.cpp)
file(GLOB_RECURSE sources_4 ${CMAKE_CURRENT_SOURCE_DIR}/obj/resize_rgbx_x86.o)
file(GLOB_RECURSE sources_5 ${CMAKE_CURRENT_SOURCE_DIR}/src/yuv2rgbx_invoke_convert.cpp)
file(GLOB_RECURSE sources_6 ${CMAKE_CURRENT_SOURCE_DIR}/obj/convert_yuv2rgbx_x86.o)
file(GLOB_RECURSE sources_7 ${CMAKE_CURRENT_SOURCE_DIR}/src/infer_sr_mlu.cpp)


add_library(${TARGET} SHARED ${sources_1} ${sources_2} ${sources_3} ${sources_4} ${sources_5} ${sources_6} ${sources_7})
message("${TARGET}")
target_link_libraries(${TARGET} PUBLIC ${SYSTEM_LIBS} ${OpenCV_LIBS} -lpthread -lm)

add_subdirectory(testcase)
