project(skin_opensource)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRCS
    skin_opensource.cpp
)

qt_add_resources(RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc)

add_library(${PROJECT_NAME} SHARED ${SRCS} ${RESOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Core Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg
    px_message px_common_new px_qt_widget skin_interface
)

# Deploy skin DLL into a px_skins/ subdirectory so SkinLoader can find them at runtime.
set(GR_SKINS_DEPLOY_DIR "${GR_PROJECT_BINARY_PATH}/px_skins")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GR_SKINS_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>" "${GR_SKINS_DEPLOY_DIR}/"
)