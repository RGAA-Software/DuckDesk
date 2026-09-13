add_library(px_ui STATIC
    components/button.cpp
    components/data_view.cpp
    components/feedback.cpp
    components/form.cpp
    components/identity.cpp
    components/navigation.cpp
    components/overlay.cpp
    components/surface.cpp
    device_platform.cpp
    localization.cpp
    px_ui_theme.cpp
    theme_tokens.cpp
    vector_icon.cpp
)

target_include_directories(px_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(px_ui PUBLIC px_imgui)

if(BUILD_TESTING)
    add_executable(px_ui_localization_tests tests/localization_tests.cpp)
    target_link_libraries(px_ui_localization_tests PRIVATE px_ui)
    add_test(NAME px_ui_localization_tests COMMAND px_ui_localization_tests)

    add_executable(px_ui_component_tests tests/component_tests.cpp)
    target_link_libraries(px_ui_component_tests PRIVATE px_ui)
    add_test(NAME px_ui_component_tests COMMAND px_ui_component_tests)
endif()
