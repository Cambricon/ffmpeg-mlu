set(SRC test_mluop.c)
get_filename_component(name "${SRC}" NAME_WE)
message("target :  ${name}")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wall -Wextra -pedantic -std=c99")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -std=c99")

file(GLOB_RECURSE algo_0 ${CMAKE_CURRENT_SOURCE_DIR}/mluop_common.cpp)
file(GLOB_RECURSE algo_1 ${CMAKE_CURRENT_SOURCE_DIR}/yuv2yuv_resize_op.cpp)
file(GLOB_RECURSE algo_2 ${CMAKE_CURRENT_SOURCE_DIR}/rgbx2rgbx_resize_op.cpp)
file(GLOB_RECURSE algo_3 ${CMAKE_CURRENT_SOURCE_DIR}/yuv2rgbx_convert_op.cpp)
file(GLOB_RECURSE algo_4 ${CMAKE_CURRENT_SOURCE_DIR}/rgbx2yuv_convert_op.cpp)
file(GLOB_RECURSE algo_5 ${CMAKE_CURRENT_SOURCE_DIR}/rgbx2rgbx_convert_op.cpp)
file(GLOB_RECURSE algo_6 ${CMAKE_CURRENT_SOURCE_DIR}/yuv2rgbx_resize_cvt_op.cpp)

add_executable(test_mluop ${SRC} ${algo_0} ${algo_1} ${algo_2} ${algo_3} ${algo_4} ${algo_5} ${algo_6})

target_link_libraries(test_mluop easyOP cnrt)
