set(SRC test_mluop.c)
get_filename_component(name "${SRC}" NAME_WE)
message("target :  ${name}")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wall -Wextra -pedantic -std=c99")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -std=c99")

add_executable(test_mluop ${SRC})

target_link_libraries(test_mluop easyOP cnrt cnml cnplugin)
