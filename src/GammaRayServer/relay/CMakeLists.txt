cmake_minimum_required(VERSION 3.26)
project(gr_relay_server)

set(CMAKE_CXX_STANDARD 20)

add_definitions(-DUNICODE -D_UNICODE)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
endif ()

find_package(Drogon CONFIG REQUIRED)

set(RELAY_SRCS
        main.cpp
        websocket/websocket_controller.cpp
        controller/device_controller.cpp
)

add_executable(${PROJECT_NAME} ${RELAY_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)