cmake_minimum_required(VERSION 3.5)
project(make_hdr LANGUAGES CXX)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

if (WIN32)
	set(OFX_PLUGIN_PATH "C:/Program Files/Common Files/OFX/Plugins")
	set(INSTALL_BIN_PATH ${OFX_PLUGIN_PATH}/make_hdr.ofx.bundle/Contents/Win64)

elseif (APPLE)
	set(OFX_PLUGIN_PATH "/Library/OFX/Plugins")
	set(INSTALL_BIN_PATH ${OFX_PLUGIN_PATH}/make_hdr.ofx.bundle/Contents/MacOS)

elseif (UNIX)
	set(OFX_PLUGIN_PATH "/usr/OFX/Plugins")
	set(INSTALL_BIN_PATH ${OFX_PLUGIN_PATH}/make_hdr.ofx.bundle/Contents/Linux-x86-64)
endif()

set(INSTALL_RES_PATH ${OFX_PLUGIN_PATH}/make_hdr.ofx.bundle/Contents/Resources)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

if (APPLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include stdio.h")
elseif (UNIX)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
endif()

include_directories(${CMAKE_SOURCE_DIR}/source)
include_directories(${CMAKE_SOURCE_DIR}/modules/openfx/include)
include_directories(${CMAKE_SOURCE_DIR}/modules/openfx/Examples/include)
include_directories(${CMAKE_SOURCE_DIR}/modules/openfx/Support/include)
include_directories(${CMAKE_SOURCE_DIR}/modules/openfx/Support/Plugins/include)
include_directories(${CMAKE_SOURCE_DIR}/modules/spdlog/include)
include_directories(${CMAKE_SOURCE_DIR}/modules/clapack/INCLUDE)
include_directories(${CMAKE_SOURCE_DIR}/modules/armadillo/include)

add_subdirectory(${CMAKE_SOURCE_DIR}/modules/clapack)
add_subdirectory(${CMAKE_SOURCE_DIR}/modules/openfx/Support/Library)

add_library(make_hdr SHARED ${CMAKE_SOURCE_DIR}/source/effect.cpp)

target_link_libraries(make_hdr f2c)
target_link_libraries(make_hdr blas)
target_link_libraries(make_hdr lapack)
target_link_libraries(make_hdr OfxSupport)

# MSVC/Windows-specific compile options and definitions.
if (WIN32 AND MSVC)
    target_compile_options(make_hdr PRIVATE /utf-8)
    target_compile_definitions(make_hdr PRIVATE NOMINMAX)

	# Workaround for constexpr mutex constructor in MSVC 1930+ (Visual Studio 2019+).
	if (MSVC_VERSION GREATER_EQUAL 1920)
		target_compile_definitions(nerk PUBLIC _CRT_SECURE_NO_WARNINGS _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
	endif()

endif()

set_target_properties(make_hdr
	PROPERTIES 
	LINKER_LANGUAGE CXX
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED YES
	PREFIX ""
	SUFFIX ".ofx"
	OUTPUT_NAME "make_hdr")

find_package(Nuke)

if (NUKE_FOUND)
	set_target_properties(make_hdr
		PROPERTIES
		VS_DEBUGGER_COMMAND ${NUKE_EXECUTABLE}
		VS_DEBUGGER_COMMAND_ARGUMENTS ${CMAKE_SOURCE_DIR}/test/room.nk
		XCODE_SCHEME_EXECUTABLE ${NUKE_EXECUTABLE}
		XCODE_SCHEME_ARGUMENTS ${CMAKE_SOURCE_DIR}/test/room.nk)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT make_hdr)

# Install the plugin binary and resource files.
install(TARGETS make_hdr DESTINATION ${INSTALL_BIN_PATH})
install(FILES ${CMAKE_SOURCE_DIR}/icons/net.sf.openfx.make_hdr.png DESTINATION ${INSTALL_RES_PATH})
install(FILES ${CMAKE_SOURCE_DIR}/icons/net.sf.openfx.make_hdr.svg DESTINATION ${INSTALL_RES_PATH})