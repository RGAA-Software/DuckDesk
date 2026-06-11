project(gr_base VERSION ${TC_APP_VERSION} LANGUAGES CXX)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/app_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/app_config.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/version_config.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/icon.ico ${CMAKE_CURRENT_BINARY_DIR}/icon.ico COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/icon.rc.in ${CMAKE_CURRENT_BINARY_DIR}/icon.rc @ONLY)

# This directory currently only provides shared headers.
# The plugin_interface library has been moved to gr_client.
