project(tc_panel_companion)

add_library(${PROJECT_NAME} panel_companion.cpp)

add_library(panel_companion SHARED
    panel_companion_impl.cpp
)
target_link_libraries(panel_companion tc_panel_companion)