set(SRC test_mluop.cpp)
get_filename_component(name "${SRC}" NAME_WE)
message("target :  ${name}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O3 -fPIC -w")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -g -O3 -fPIC -w")
set (CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_CXX_LINK_EXECUTABLE} -ldl")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB_RECURSE algo_1 ${CMAKE_CURRENT_SOURCE_DIR}/yuv2yuv_resize_op.cpp)
file(GLOB_RECURSE algo_2 ${CMAKE_CURRENT_SOURCE_DIR}/rgbx2rgbx_resize_op.cpp)
file(GLOB_RECURSE algo_3 ${CMAKE_CURRENT_SOURCE_DIR}/yuv2rgbx_convert_op.cpp)
file(GLOB_RECURSE algo_4 ${CMAKE_CURRENT_SOURCE_DIR}/rgbx2yuv_convert_op.cpp)
file(GLOB_RECURSE algo_5 ${CMAKE_CURRENT_SOURCE_DIR}/rgbx2rgbx_convert_op.cpp)
file(GLOB_RECURSE algo_6 ${CMAKE_CURRENT_SOURCE_DIR}/yuv2rgbx_resize_cvt_op.cpp)

add_executable(test_mluop ${SRC} ${algo_0} ${algo_1} ${algo_2} ${algo_3} ${algo_4} ${algo_5} ${algo_6})

target_link_libraries(test_mluop cnrt cncv easyOP)
