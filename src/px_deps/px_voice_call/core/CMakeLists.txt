cmake_minimum_required(VERSION 3.26)

# Reused by the full voice engine and SDK-only consumers, without selecting audio devices/APM.
add_library(px_voice_call_core STATIC
    ../voice_call_state.cpp
    ../voice_packet_transport.cpp)
target_compile_features(px_voice_call_core PUBLIC cxx_std_23)
set_target_properties(px_voice_call_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(px_voice_call_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(px_voice_call_core PUBLIC px_common_async px_common_crypto)
