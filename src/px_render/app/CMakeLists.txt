if (WIN32)
    add_library(app_manager
        app_manager.cpp
        app_manager_factory.cpp
        app_timer.cpp
        app_shared_info.cpp
        steam_game.cpp
        win/app_manager_win.cpp
        win/dx_address_loader.cpp
        #win/win_event_replayer.cpp
        win/win_render_message_window.cpp
        win/win_desktop_manager.cpp
        win/win_render_message_loop.cpp
    )
    target_link_libraries(app_manager px_message protobuf::libprotobuf Advapi32.lib)#frame_render
elseif (APPLE)

elseif (UNIX)

endif ()

