find_package(unofficial-libvpx CONFIG REQUIRED)
#find_package(libyuv CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${GR_PROJECT_PATH})

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

#aux_source_directory(. SRCS)

#include_directories(${GR_PROJECT_PATH}/FFmpeg-5.1/x64/include)
set(NV_SRCS
	D3DTextureDebug.cpp
	EncodeDataCallback.h
	#NvEncoder.cpp
	#NvEncoderD3D11.cpp
	VideoEncoder.cpp
	#VideoEncoderNVENC.cpp
	VideoEncoderVCE.cpp
)
add_library(legacy_encoder ${NV_SRCS} ${EXT_SRCS})
target_link_libraries(legacy_encoder encoder_shared tc_common_new)
#target_link_libraries(encoder unofficial::libvpx::libvpx yuv Opus::opus)
#target_link_directories(encoder PUBLIC ${GR_PROJECT_PATH}/FFmpeg-5.1/x64/lib)
#target_link_libraries(encoder avcodec avdevice avfilter avformat avutil postproc swresample swscale)


#add_custom_command(TARGET encoder PRE_BUILD
	#COMMAND "${CMAKE_COMMAND}" -E copy "${GR_PROJECT_PATH}/directxtk/lib/x64/DirectXTK.lib" ${CMAKE_BINARY_DIR}
	#VERBATIM
#)