if (WIN32)
    # Legacy C++ GammaRayGuard sources.
    # The active build now stages rust/gr_guard -> GammaRayGuard.exe from src/GammaRay/CMakeLists.txt.
    set(GUARD_SRCS
            ${CMAKE_CURRENT_SOURCE_DIR}/gr_guard_main.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/gr_panel_guard.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/gr_guard_context.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/gr_guard_app.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/gr_panel_client.cpp
            PARENT_SCOPE
    )
else ()

endif ()
