add_library(px_ui STATIC
    localization.cpp
    px_ui_theme.cpp
)

target_include_directories(px_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(px_ui PUBLIC px_imgui)

if(BUILD_TESTING)
    add_executable(px_ui_localization_tests tests/localization_tests.cpp)
    target_link_libraries(px_ui_localization_tests PRIVATE px_ui)
    add_test(NAME px_ui_localization_tests COMMAND px_ui_localization_tests)
endif()
