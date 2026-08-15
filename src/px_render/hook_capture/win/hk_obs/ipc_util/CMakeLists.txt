cmake_minimum_required(VERSION 3.26)
project(px_ipc_util)
add_library(${PROJECT_NAME}
        pipe-windows.c
)