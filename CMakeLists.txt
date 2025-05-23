cmake_minimum_required(VERSION 3.5)
project(make_hdr LANGUAGES CXX)

set(CMAKE_BUILD_TYPE Release)
set(INSTALL_BIN_PATH ${CMAKE_BINARY_DIR}/make_hdr.ofx.bundle/Contents)
set(INSTALL_RES_PATH ${INSTALL_BIN_PATH}/Resources)

if (WIN32)
	set(INSTALL_BIN_PATH "${INSTALL_BIN_PATH}/Win64/")
elseif (APPLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include stdio.h")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
	set(INSTALL_BIN_PATH "${INSTALL_BIN_PATH}/MacOS/")
elseif (UNIX)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
	set(INSTALL_BIN_PATH "${INSTALL_BIN_PATH}/Linux-x86-64/")
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

# Require C++14 standard for this target.
set_target_properties(make_hdr PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES)

target_link_libraries(make_hdr f2c)
target_link_libraries(make_hdr blas)
target_link_libraries(make_hdr lapack)
target_link_libraries(make_hdr OfxSupport)

set_target_properties(make_hdr
	PROPERTIES 
	LINKER_LANGUAGE CXX
	PREFIX ""
	SUFFIX ".ofx"
	OUTPUT_NAME "make_hdr")

# MSVC/Windows-specific compile options and definitions.
if (WIN32 AND MSVC)
    target_compile_options(make_hdr PRIVATE /utf-8)
    target_compile_definitions(make_hdr PRIVATE NOMINMAX)
endif()

# Workaround for constexpr mutex constructor in MSVC 1930+ (Visual Studio 2019+).
if (MSVC AND MSVC_VERSION GREATER_EQUAL 1930)
    target_compile_definitions(make_hdr PRIVATE _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

# Install the plugin binary and resource files.
install(FILES $<TARGET_FILE:make_hdr> DESTINATION ${INSTALL_BIN_PATH})
install(FILES ${CMAKE_SOURCE_DIR}/icons/net.sf.openfx.make_hdr.png DESTINATION ${INSTALL_RES_PATH})
install(FILES ${CMAKE_SOURCE_DIR}/icons/net.sf.openfx.make_hdr.svg DESTINATION ${INSTALL_RES_PATH})