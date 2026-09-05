set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PX_PROJECT_PATH})

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

set(NV_SRCS
	D3DTextureDebug.cpp
	EncodeDataCallback.h
	VideoEncoder.cpp
	VideoEncoderVCE.cpp
)

# Keep the read-only AMD SDK support layer on its compatible language level;
# GammaRay-owned encoder code remains on the project C++23 standard.
add_library(amf_legacy_sdk STATIC ${EXT_SRCS})
set_target_properties(amf_legacy_sdk PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

add_library(legacy_encoder ${NV_SRCS})
target_link_libraries(legacy_encoder PRIVATE encoder_shared px_common amf_legacy_sdk)
