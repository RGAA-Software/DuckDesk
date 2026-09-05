project(enc_amf)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(shared)

set(EXT_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/Windows/ThreadWindows.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/AMFFactory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/AMFSTL.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/Thread.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/TraceAdapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/CurrentTimeImpl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/DataStreamFactory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/DataStreamFile.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/DataStreamMemory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/IOCapsImpl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/amf/common/PropertyStorageExImpl.cpp
)

set(MODULE_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/amf_video_encoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/d3d_texture_debug.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/encode_debug_callback.h
        ${CMAKE_CURRENT_SOURCE_DIR}/video_encoder_vce.cpp
)

add_library(${PROJECT_NAME} STATIC ${MODULE_SRCS} ${EXT_SRCS})
target_compile_definitions(${PROJECT_NAME} PRIVATE _HAS_DEPRECATED_ALLOCATOR_MEMBERS=1)
target_link_libraries(${PROJECT_NAME} PRIVATE px_common render_architecture_core winmm.lib amf_encoder_shared SetupAPI.lib)
