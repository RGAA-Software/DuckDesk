project(plugin_frame_carrier)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/frame_carrier_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/video_frame_carrier.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/image_generator.cpp
)

qt_add_resources(RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS} ${RESOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE tc_common_new tc_steam_manager_new tc_message tc_plugin winmm.lib SetupAPI.lib yuv Qt6::Core Qt6::Widgets)
#target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_Libs})
