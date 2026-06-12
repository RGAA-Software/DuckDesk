cmake_minimum_required(VERSION 3.26)
project(tc_graphics_lb)

set(CMAKE_CXX_STANDARD 23)

set(SRCS
    dll_main.cpp
    ../client_manager.cpp
)
add_library(${PROJECT_NAME} SHARED ${SRCS})
# easyhook does not have a static triplet, keep using the dynamic x64-windows package
target_include_directories(${PROJECT_NAME} PRIVATE ${VCPKG_ROOT}/installed/x64-windows/include)
target_link_directories(${PROJECT_NAME} PRIVATE ${VCPKG_ROOT}/installed/x64-windows/lib)
target_link_libraries(${PROJECT_NAME} tc_common_new EasyHook64)