set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PX_PROJECT_PATH}/px_message_new)
include_directories(${PX_PROJECT_PATH}/px_3rdparty/)

set(OPUS_SRCS opus_codec.cpp)

add_library(px_opus_codec_new ${OPUS_SRCS} ${EXT_SRCS})
target_link_libraries(px_opus_codec_new px_common_new Opus::opus)
