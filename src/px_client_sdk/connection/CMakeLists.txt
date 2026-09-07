set(CONN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/connection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/udp_connection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/udp_direct_connection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ws_connection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/wss_connection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relay_connection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_ice_restart_workflow.cpp
)

if(NOT DEFINED PX_RTC_TRANSPORT_AVAILABLE OR PX_RTC_TRANSPORT_AVAILABLE)
    list(APPEND CONN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_connection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_local_connection.cpp
    )
endif()

set(CONN_SRCS ${CONN_SRCS} PARENT_SCOPE)
