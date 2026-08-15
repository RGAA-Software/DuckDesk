add_subdirectory(opensource)
add_subdirectory(official)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SKIN_LOADER_SRCS
    skin_loader.cpp
)

add_library(skin_loader ${SKIN_LOADER_SRCS} ${RESOURCES})
target_link_libraries(skin_loader PRIVATE Qt6::Widgets Qt6::Core Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg
    px_message px_common_new px_qt_widget skin_interface
)