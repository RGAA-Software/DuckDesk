project(skin_official)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRCS
    skin_official.cpp
)

qt_add_resources(RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc)

add_library(${PROJECT_NAME} SHARED ${SRCS} ${RESOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Core Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg
    px_message px_common_new px_qt_widget skin_interface
)

# Build the skin DLL directly into px_skins/ so SkinLoader finds it at runtime
# (and so no stray skin DLL lands in the outer build dir).
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}/px_skins"
    ARCHIVE_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}/px_skins"
)

# Deploy the skin config next to the DLL.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PX_PROJECT_BINARY_PATH}/px_skins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../skin_config.toml" "${PX_PROJECT_BINARY_PATH}/px_skins/"
)
