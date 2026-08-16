project(multi_screens)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/multi_screens_plugin.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_client_plugin Qt6::Core Qt6::Widgets)
target_include_directories(${PROJECT_NAME} PRIVATE
    
    ${CMAKE_SOURCE_DIR}/src/px_client/plugin_interface
)