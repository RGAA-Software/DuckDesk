if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "optional_copy.cmake requires SRC and DST")
endif()

if(NOT EXISTS "${SRC}")
    message(WARNING "Optional copy skipped, source does not exist: ${SRC}")
    return()
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy "${SRC}" "${DST}"
    RESULT_VARIABLE copy_result
    ERROR_VARIABLE copy_error
)

if(NOT copy_result EQUAL 0)
    string(STRIP "${copy_error}" copy_error)
    message(WARNING "Optional copy failed: ${SRC} -> ${DST}. ${copy_error}")
endif()
