cmake_minimum_required(VERSION 3.13)

# Default to release build
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type: Release|Debug")

project(HydraNSI
	LANGUAGES CXX
	DESCRIPTION "Hydra render plugin for 3Delight")

list(APPEND CMAKE_MODULE_PATH
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# Default install prefix is a directory in the build one.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/output" CACHE PATH "..." FORCE)
endif()

# Shut up 'Up-to-date' install messages.
set(CMAKE_INSTALL_MESSAGE LAZY)

if(WIN32)
	# Fix Windows. I can dream, right?
	add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

add_subdirectory(hdNSI)
