set(GR_RENDER_PATH ${CMAKE_SOURCE_DIR}/src/gr_render CACHE PATH "Path to gr_render sources")
set(GR_PROJECT_PATH ${CMAKE_SOURCE_DIR}/src/GammaRay)

include(${GR_PROJECT_PATH}/version.cmake)

add_subdirectory(GammaRay)
add_subdirectory(gr_render)
#add_subdirectory(GammaRayServer)
add_subdirectory(client_plugins)
add_subdirectory(render_plugins)
add_subdirectory(hook_capture)
add_subdirectory(panel_companion)
add_subdirectory(skins)
add_subdirectory(anti_hooking)
