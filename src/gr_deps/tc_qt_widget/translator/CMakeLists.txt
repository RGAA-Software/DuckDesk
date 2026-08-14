project(tc_translator)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Core)

add_library(${PROJECT_NAME}
        tc_translator.cpp
)

target_link_libraries(${PROJECT_NAME} Qt6::Core Qt6::Widgets)