cmake_minimum_required(VERSION 3.5)
project(make_hdr LANGUAGES CXX)

set(CMAKE_BUILD_TYPE Release)
set(INSTALL_BIN_PATH ${CMAKE_BINARY_DIR}/make_hdr.ofx.bundle/Contents)
set(INSTALL_RES_PATH ${INSTALL_BIN_PATH}/Resources)

include_directories(${CMAKE_SOURCE_DIR}/source)
include_directories(${CMAKE_SOURCE_DIR}/modules/openfx/include)
include_directories(${CMAKE_SOURCE_DIR}/modules/openfx/Examples/include)
include_directories(${CMAKE_SOURCE_DIR}/modules/openfx/Support/include)
include_directories(${CMAKE_SOURCE_DIR}/modules/openfx/Support/Plugins/include)
include_directories(${CMAKE_SOURCE_DIR}/modules/spdlog/include)
include_directories(${CMAKE_SOURCE_DIR}/modules/clapack/INCLUDE)
include_directories(${CMAKE_SOURCE_DIR}/modules/armadillo/include)

add_library(make_hdr SHARED ${CMAKE_SOURCE_DIR}/source/effect.cpp)

if (UNIX)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
	
	if(APPLE)
		set(LIB_DIR "${CMAKE_SOURCE_DIR}/lib/mac")
		set(INSTALL_BIN_PATH "${INSTALL_BIN_PATH}/MacOS/")
	else()
		set(LIB_DIR "${CMAKE_SOURCE_DIR}/lib/linux")
		set(INSTALL_BIN_PATH "${INSTALL_BIN_PATH}/Linux-x86-64/")
	endif()

	target_link_libraries(make_hdr ${LIB_DIR}/libOfxSupport.a)
	target_link_libraries(make_hdr ${LIB_DIR}/liblapack.a)
	target_link_libraries(make_hdr ${LIB_DIR}/libblas.a)
	target_link_libraries(make_hdr ${LIB_DIR}/libf2c.a)

elseif (WIN32)
	set(INSTALL_BIN_PATH "${INSTALL_BIN_PATH}/Win64/")
	SET(CMAKE_CONFIGURATION_TYPES ${CMAKE_BUILD_TYPE} CACHE STRING "" FORCE)
	target_link_libraries(make_hdr ${CMAKE_SOURCE_DIR}/lib/win/OfxSupport.lib)
	target_link_libraries(make_hdr ${CMAKE_SOURCE_DIR}/lib/win/lapack.lib)
	target_link_libraries(make_hdr ${CMAKE_SOURCE_DIR}/lib/win/blas.lib)
	target_link_libraries(make_hdr ${CMAKE_SOURCE_DIR}/lib/win/libf2c.lib)
endif()

set_target_properties(make_hdr
	PROPERTIES 
	LINKER_LANGUAGE CXX
	PREFIX ""
	SUFFIX ".ofx"
	OUTPUT_NAME "make_hdr")

target_compile_definitions(make_hdr PRIVATE)

install(TARGETS make_hdr DESTINATION ${INSTALL_BIN_PATH})
install(FILES ${CMAKE_SOURCE_DIR}/icons/net.sf.openfx.make_hdr.png DESTINATION ${INSTALL_RES_PATH})
install(FILES ${CMAKE_SOURCE_DIR}/icons/net.sf.openfx.make_hdr.svg DESTINATION ${INSTALL_RES_PATH})