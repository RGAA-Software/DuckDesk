project(tc_qt_widget)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core OpenGL OpenGLWidgets Svg)

add_subdirectory(translator)
#add_subdirectory(widgetframe)
add_subdirectory(loadings)

add_compile_options(/bigobj)
set(SRCS
    round_img_display.cpp
    single_selected_list.cpp
    clickable_widget.cpp
    cover_widget.cpp
    sized_msg_box.cpp
    custom_tab_btn.cpp
    tc_image_button.cpp
    tc_qr_widget.cpp
    tc_label.cpp
    tc_font_manager.cpp
    tc_pushbutton.cpp
    tc_custom_titlebar.cpp
    tc_custom_titlebar_dialog.cpp
    tc_dialog.cpp
    tc_password_input.cpp
    tc_tooltip.cpp
    tc_base_widget.cpp
    tc_circle_indicator.cpp
    tc_dialog_util.cpp

    notify/arrangedwnd.cpp
    notify/notifymanager.cpp
    notify/notifywnd.cpp

    pagination/page_widget.cpp
    image_cropper/image_cropper_label.cpp
)

add_library(${PROJECT_NAME} ${SRCS})
if (APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER com.tc.qt.widget
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
            MACOSX_BUNDLE TRUE
            WIN32_EXECUTABLE TRUE
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets
        Qt6::Core
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Svg
)
target_link_libraries(${PROJECT_NAME} PRIVATE tc_common_new tc_translator tc_widget_loadings)