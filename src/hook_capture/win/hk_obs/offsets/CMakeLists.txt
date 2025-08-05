cmake_minimum_required(VERSION 3.26)
project(tc_graphics_helper)

set(SRCS d3d8-offsets.cpp d3d9-offsets.cpp ddraw-offsets.cpp dxgi-offsets.cpp get-graphics-offsets.cpp)
add_executable(${PROJECT_NAME} ${SRCS})