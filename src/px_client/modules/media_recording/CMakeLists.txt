project(client_media_recording_module)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MODULE_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/media_recording_module.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/media_recorder.cpp
)

add_library(${PROJECT_NAME} STATIC ${MODULE_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE
        px_client_module_runtime px_common_new px_message
        px_media_record_new Qt6::Core)
target_include_directories(${PROJECT_NAME} PRIVATE

    ${CMAKE_SOURCE_DIR}/src/px_client
)
