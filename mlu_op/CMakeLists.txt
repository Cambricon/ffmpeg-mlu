cmake_minimum_required(VERSION 3.0)

project(EasyOP C CXX)

set(TARGET easyOP)

option(RELEASE "build type" ON)
option(TEST_CASE "build type" OFF)
option(IS_CROSS_COMPILE "option for cross compile" OFF)

# -- Cross Compile
if(IS_CROSS_COMPILE)
  set(tool_path /usr/local/bin/gcc-linaro-6.5.0-2018.12-x86_64_aarch64-linux-gnu)
  set(CROSS_PREFIX ${tool_path}/bin/aarch64-linux-gnu-)
  set(CMAKE_C_COMPILER ${tool_path}/bin/aarch64-linux-gnu-gcc)
  set(CMAKE_CXX_COMPILER ${tool_path}/bin/aarch64-linux-gnu-g++)

  set(CMAKE_CROSSCOMPILING TRUE)
  set(CMAKE_SYSTEM_NAME "Linux")
  set(CMAKE_SYSTEM_PROCESSOR aarch64)

  set(CMAKE_FIND_ROOT_PATH ${tool_path}/bin)
  set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
  set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
  set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
endif()

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

if(TEST_CASE)
  find_package( OpenCV REQUIRED )
endif()

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib/)
set(CNCV_LDLIBRARY $ENV{NEUWARE_HOME}/lib64/libcncv.so)

link_directories("$ENV{NEUWARE_HOME}/lib64")
link_directories(${PROJECT_SOURCE_DIR}/lib)
include_directories($ENV{NEUWARE_HOME}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SYSTEM_LIBS cnrt)

file(GLOB_RECURSE sources_1 ${CMAKE_CURRENT_SOURCE_DIR}/src/yuv2yuv_resize.cpp)
file(GLOB_RECURSE sources_2 ${CMAKE_CURRENT_SOURCE_DIR}/src/rgbx2rgbx_resize.cpp)
file(GLOB_RECURSE sources_3 ${CMAKE_CURRENT_SOURCE_DIR}/src/yuv2rgbx_convert.cpp)
file(GLOB_RECURSE sources_4 ${CMAKE_CURRENT_SOURCE_DIR}/src/rgbx2yuv_convert.cpp)

add_library(${TARGET} SHARED ${sources_1} ${sources_2} ${sources_3} ${sources_4})
message("${TARGET}")
if(TEST_CASE)
  target_link_libraries(${TARGET} PUBLIC ${SYSTEM_LIBS} ${OpenCV_LIBS} ${CNCV_LDLIBRARY} -lpthread -lm)
  add_subdirectory(testcase)
else()
  target_link_libraries(${TARGET} PUBLIC ${SYSTEM_LIBS} ${CNCV_LDLIBRARY} -lpthread -lm)
endif()
