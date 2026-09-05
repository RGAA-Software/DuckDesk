cmake_minimum_required(VERSION 3.22)

include_directories(${GR_PROJECT_PATH}/px_3rdparty/)

# One canonical standalone Asio installation is shared by asio2 and project
# infrastructure such as MessageNotifier. Keeping these options on an
# INTERFACE target prevents consumers from selecting incompatible Asio modes.
if(NOT TARGET px_asio)
	add_library(px_asio INTERFACE)
	add_library(px::asio ALIAS px_asio)
	target_include_directories(px_asio SYSTEM INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}/asio2/3rd
	)
	target_compile_definitions(px_asio INTERFACE
		ASIO_STANDALONE
		ASIO_HEADER_ONLY
		PX_ASIO_VERSION=103802
	)
endif()

if(NOT TARGET px_asio2)
	add_library(px_asio2 INTERFACE)
	add_library(px::asio2 ALIAS px_asio2)
	target_include_directories(px_asio2 SYSTEM INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}/asio2/include
	)
	target_link_libraries(px_asio2 INTERFACE px::asio)
endif()

if(WIN32)

	if(DirectXTex_ENABLED)
		#add_subdirectory(DirectXTex)
	endif()

	# qwindowkit
	#set(QWINDOWKIT_BUILD_EXAMPLES OFF)
	#add_subdirectory(qwindowkit/qmsetup)
	#add_subdirectory(qwindowkit)

	#libyuv
	#add_subdirectory(libyuv)
endif()

#set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "LEVELDB_BUILD_TESTS OFF" FORCE)
#set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "LEVELDB_BUILD_TESTS OFF" FORCE)
#add_subdirectory(leveldb)
#add_subdirectory(sqlite35)
