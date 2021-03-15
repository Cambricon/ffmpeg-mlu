cmake_minimum_required(VERSION 2.8)

project(ffmpeg-mlu_video_encoder)

set(TARGET video_encoder)

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

link_directories("$ENV{NEUWARE_HOME}/lib64")

include_directories(${CMAKE_SOURCE_DIR}/3rdparty/ffmpeg/include)
set(FFMPEG_LIBRARIES ${PROJECT_SOURCE_DIR}/3rdparty/ffmpeg/lib/libavcodec.so)
list(APPEND FFMPEG_LIBRARIES ${PROJECT_SOURCE_DIR}/3rdparty/ffmpeg/lib/libavdevice.so)
list(APPEND FFMPEG_LIBRARIES ${PROJECT_SOURCE_DIR}/3rdparty/ffmpeg/lib/libavfilter.so)
list(APPEND FFMPEG_LIBRARIES ${PROJECT_SOURCE_DIR}/3rdparty/ffmpeg/lib/libavformat.so)
list(APPEND FFMPEG_LIBRARIES ${PROJECT_SOURCE_DIR}/3rdparty/ffmpeg/lib/libavutil.so)
list(APPEND FFMPEG_LIBRARIES ${PROJECT_SOURCE_DIR}/3rdparty/ffmpeg/lib/libswresample.so)
list(APPEND FFMPEG_LIBRARIES ${PROJECT_SOURCE_DIR}/3rdparty/ffmpeg/lib/libswscale.so)
set(SYSTEM_LIBS pthread dl cncodec cnrt)

file(GLOB_RECURSE sources src/*.[ch]pp src/*.h)
list(REMOVE_ITEM sources ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

add_executable(${TARGET} ${sources} ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

target_link_libraries(${TARGET} PUBLIC ${FFMPEG_LIBRARIES} ${SYSTEM_LIBS})

