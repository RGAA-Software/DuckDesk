project(skin_interface)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRCS
    skin_context.cpp
    skin_interface.cpp
)

add_library(${PROJECT_NAME} ${SRCS})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_BINARY_DIR}/src/px_base)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Core Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg px_common_new)
