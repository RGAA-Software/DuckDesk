# Build web/gr_web_client via npm. Invoked from CMakeLists.txt as:
#   cmake -DNPM_EXECUTABLE=... -DWEB_DIR=... -P scripts/build_gr_web_client.cmake

if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "NPM_EXECUTABLE is not set")
endif()
if(NOT WEB_DIR OR NOT IS_DIRECTORY "${WEB_DIR}")
    message(FATAL_ERROR "WEB_DIR is missing or not a directory: '${WEB_DIR}'")
endif()

# Always wipe previous vite output so hashed assets cannot linger.
if(EXISTS "${WEB_DIR}/dist")
    message(STATUS "Removing stale ${WEB_DIR}/dist")
    file(REMOVE_RECURSE "${WEB_DIR}/dist")
endif()

# Always sync deps so package.json additions cannot leave a stale node_modules.
message(STATUS "npm ci in ${WEB_DIR}")
execute_process(
    COMMAND "${NPM_EXECUTABLE}" ci
    WORKING_DIRECTORY "${WEB_DIR}"
    RESULT_VARIABLE _npm_rc
)
if(_npm_rc)
    message(STATUS "npm ci failed, falling back to npm install")
    execute_process(
        COMMAND "${NPM_EXECUTABLE}" install
        WORKING_DIRECTORY "${WEB_DIR}"
        RESULT_VARIABLE _npm_rc
    )
    if(_npm_rc)
        message(FATAL_ERROR "npm install failed in ${WEB_DIR} (exit ${_npm_rc})")
    endif()
endif()
message(STATUS "npm run build in ${WEB_DIR}")
execute_process(
    COMMAND "${NPM_EXECUTABLE}" run build
    WORKING_DIRECTORY "${WEB_DIR}"
    RESULT_VARIABLE _npm_rc
)
if(_npm_rc)
    message(FATAL_ERROR "npm run build failed in ${WEB_DIR} (exit ${_npm_rc})")
endif()

if(NOT EXISTS "${WEB_DIR}/dist/index.html")
    message(FATAL_ERROR "web client build did not produce dist/index.html in ${WEB_DIR}")
endif()