project(px_qt_widget)

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
    px_image_button.cpp
    px_qr_widget.cpp
    px_label.cpp
    px_font_manager.cpp
    px_pushbutton.cpp
    px_custom_titlebar.cpp
    px_custom_titlebar_dialog.cpp
    px_dialog.cpp
    px_password_input.cpp
    px_tooltip.cpp
    px_base_widget.cpp
    px_circle_indicator.cpp
    px_dialog_util.cpp

    notify/arrangedwnd.cpp
    notify/notifymanager.cpp
    notify/notifywnd.cpp

    pagination/page_widget.cpp
    image_cropper/image_cropper_label.cpp
)

add_library(${PROJECT_NAME} ${SRCS})
if (APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER com.px.qt.widget
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
target_link_libraries(${PROJECT_NAME} PRIVATE px_common px_translator px_widget_loadings)

add_executable(test_notify_lifecycle test/test_notify_lifecycle.cpp)
target_link_libraries(test_notify_lifecycle PRIVATE
        px_qt_widget GTest::gtest Qt6::Core Qt6::Widgets)
if (WIN32)
    add_custom_command(TARGET test_notify_lifecycle POST_BUILD
        COMMAND ${QT_ROOT}/bin/windeployqt.exe
            $<TARGET_FILE:test_notify_lifecycle>
            --no-translations --release
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_ROOT}/plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:test_notify_lifecycle>/platforms/qoffscreen.dll")
endif ()
