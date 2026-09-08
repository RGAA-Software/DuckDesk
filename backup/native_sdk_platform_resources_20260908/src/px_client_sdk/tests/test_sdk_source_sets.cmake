cmake_minimum_required(VERSION 3.26)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/sdk_source_sets.cmake")
get_filename_component(sdk_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(PX_SDK_TEST_UNSUPPORTED)
    px_sdk_platform_sources(unimplemented_sources macos)
    return()
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -DPX_SDK_TEST_UNSUPPORTED=ON -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE unsupported_result
    OUTPUT_QUIET
    ERROR_VARIABLE unsupported_error
)
if(unsupported_result EQUAL 0 OR NOT unsupported_error MATCHES "No SDK decoder adapter")
    message(FATAL_ERROR "An unimplemented platform must fail explicitly, not receive a fallback decoder")
endif()

px_sdk_platform_sources(windows_sources windows)
px_sdk_platform_sources(android_sources android)

# Check both adapters on either build host. No compiler, Qt, FFmpeg installation
# or Android NDK is needed for this source-boundary regression.
if(NOT windows_sources STREQUAL "sdk_ffmpeg_decoder.cpp;sdk_ffmpeg_vulkan_decoder.cpp;sdk_ffmpeg_soft_decoder.cpp;platform/windows/windows_decoder_factory.cpp")
    message(FATAL_ERROR "Windows decoder selection changed; review backend isolation")
endif()
if(NOT android_sources STREQUAL "sdk_mediacodec_video_decoder.cpp;sdk_android_software_decoder.cpp;platform/android/android_decoder_factory.cpp")
    message(FATAL_ERROR "Android decoder selection changed; review backend isolation")
endif()

set(all_sources ${PX_SDK_CORE_SOURCES} ${PX_SDK_SESSION_SOURCES} ${windows_sources} ${android_sources})
list(LENGTH all_sources source_count)
list(REMOVE_DUPLICATES all_sources)
list(LENGTH all_sources unique_count)
if(NOT source_count EQUAL unique_count)
    message(FATAL_ERROR "SDK source sets overlap: an implementation would be built twice")
endif()
foreach(source IN LISTS PX_SDK_CORE_SOURCES)
    if(source MATCHES "decoder|raw_image|thunder_sdk")
        message(FATAL_ERROR "Platform/session implementation leaked into shared core: ${source}")
    endif()
endforeach()

# Follow SDK-owned headers as well as direct includes: a neutral-looking header
# must not silently reintroduce the session's FFmpeg/D3D11/Surface parameter bag.
function(check_core_includes source)
    file(REAL_PATH "${source}" source)
    get_property(visited GLOBAL PROPERTY PX_SDK_CHECKED_CORE_INCLUDES)
    if(source IN_LIST visited)
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY PX_SDK_CHECKED_CORE_INCLUDES "${source}")
    if(source MATCHES "/(sdk_params\\.h|thunder_sdk\\.h|sdk_.*decoder.*\\.h|gl/.*)$")
        message(FATAL_ERROR "Platform/session header leaked into transport core: ${source}")
    endif()
    file(READ "${source}" contents)
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[\"<][^\">]+[\">]" includes "${contents}")
    get_filename_component(source_dir "${source}" DIRECTORY)
    foreach(include IN LISTS includes)
        string(REGEX REPLACE "^#[ \t]*include[ \t]*[\"<]([^\">]+)[\">]$" "\\1" header "${include}")
        foreach(base IN ITEMS "${source_dir}" "${sdk_root}" "${sdk_root}/..")
            if(EXISTS "${base}/${header}")
                file(REAL_PATH "${base}/${header}" candidate)
                cmake_path(IS_PREFIX sdk_root "${candidate}" NORMALIZE is_sdk_header)
                if(is_sdk_header)
                    check_core_includes("${candidate}")
                endif()
                break()
            endif()
        endforeach()
    endforeach()
endfunction()
foreach(source IN LISTS PX_SDK_CORE_SOURCES)
    check_core_includes("${sdk_root}/${source}")
endforeach()

foreach(source IN ITEMS thunder_sdk.h thunder_sdk.cpp sdk_video_decoder.h sdk_video_decoder.cpp sdk_video_decoder_factory.h)
    file(READ "${sdk_root}/${source}" contents)
    if(contents MATCHES "sdk_ffmpeg|sdk_mediacodec|sdk_android_software|DecoderRenderType|render_surface_handle_|void[ \t]*\\*[ \t]*surface|UpdateRenderSurface")
        message(FATAL_ERROR "Concrete decoder or erased Surface boundary leaked into the session API: ${source}")
    endif()
endforeach()

# Inventory only, never automatic source discovery for a product target.
file(GLOB present_sources RELATIVE "${sdk_root}"
    "${sdk_root}/*.cpp" "${sdk_root}/connection/*.cpp" "${sdk_root}/gl/*.cpp" "${sdk_root}/platform/*/*.cpp")
list(SORT present_sources)
list(SORT all_sources)
if(NOT all_sources STREQUAL present_sources)
    message(FATAL_ERROR "SDK source inventory differs from its explicit build sets\nDeclared: ${all_sources}\nPresent: ${present_sources}")
endif()
message(STATUS "PASS: SDK source sets are disjoint/complete; transport headers exclude session and decoder types")
