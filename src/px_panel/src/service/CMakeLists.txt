# Active directory contract:
# - `service_manager.cpp` remains the live C++ wrapper used by the panel code.
# - The original C++ service implementation has been moved to `legacy/`.
# - Rust `px_service.exe` / `px_service_manager.exe` are the only
#   service executables that should be built.
#
# Keep the historical source list commented out here to make that switch
# explicit and to show which files were replaced.
#
# set(SERVICE_SRCS
#         ${CMAKE_CURRENT_SOURCE_DIR}/legacy/service_main.cpp
#         ${CMAKE_CURRENT_SOURCE_DIR}/legacy/service.cpp
#         ${CMAKE_CURRENT_SOURCE_DIR}/legacy/service_context.cpp
#         ${CMAKE_CURRENT_SOURCE_DIR}/legacy/service_msg_server.cpp
#         ${CMAKE_CURRENT_SOURCE_DIR}/legacy/render_manager.cpp
#         PARENT_SCOPE
# )

set(SERVICE_MGR_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/service_manager.cpp
        PARENT_SCOPE
)

# The standalone C++ px_service_manager executable is also disabled.
# Rust `px_service_manager.exe` replaces it, while `service_manager.cpp`
# remains as the panel-facing wrapper used by the existing C++ application.
#
# set(SERVICE_MGR_MAIN_SRCS
#         ${CMAKE_CURRENT_SOURCE_DIR}/legacy/service_manager_main.cpp
#         PARENT_SCOPE
# )
