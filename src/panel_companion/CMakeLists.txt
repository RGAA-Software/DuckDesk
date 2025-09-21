project(tc_panel_companion)

add_library(panel_companion SHARED
    panel_companion_impl.cpp
    spvr/auth_manager.cpp
    spvr/spvr_setting.cpp
    crypto/auth_aes.cpp
    hw_info_parser.cpp
)
target_link_libraries(panel_companion tc_panel_companion tc_common_new)