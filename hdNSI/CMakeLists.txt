# Find pxr (Pixar's USD)
find_package(pxr CONFIG)

if(NOT pxr_FOUND)
	# Try to find USD as part of Houdini.
	find_package(HoudiniUSD)

	if(NOT HoudiniUSD_FOUND)
		# Try to find USD as part of Katana.
		find_package(KatanaUSD)
	endif()
endif()

if(NOT pxr_FOUND AND NOT HoudiniUSD_FOUND AND NOT KatanaUSD_FOUND)
	message(FATAL_ERROR
		"Required: USD install or Houdini or Katana with included USD.")
endif()

find_package(3Delight REQUIRED)

set(LIB_TARGET ${HYDRANSI_TARGET_PREFIX}hdNSI)
add_library(
	${LIB_TARGET} SHARED

	camera.cpp
	cameraData.cpp
	curves.cpp
	discoveryPlugin.cpp
	field.cpp
	light.cpp
	materialAssign.cpp
	material.cpp
	mesh.cpp
	osoParserPlugin.cpp
	outputDriver.cpp
	pointcloud.cpp
	pointInstancer.cpp
	primvars.cpp
	renderBuffer.cpp
	renderDelegate.cpp
	rendererPlugin.cpp
	renderPass.cpp
	rprimBase.cpp
	tokens.cpp
	volume.cpp
	)

if(PXR_VERSION GREATER_EQUAL "2205")
	target_sources(${LIB_TARGET} PRIVATE
		accelerationBlurPlugin.cpp
		)
	target_compile_definitions(${LIB_TARGET} PRIVATE ENABLE_ABP)
endif()

# Using alternate target name should not change library name.
# Alghouth I think that would be ok as long as plugInfo.json matches.
set_target_properties(${LIB_TARGET} PROPERTIES OUTPUT_NAME hdNSI)

# By default, we set the standard to C++14 which is required by OpenUSD
# versions prior to 23.11.
set(LIB_CXX_STANDARD 14)

# OpenUSD 23.11 updated C++ standard version to C++17 per
# VFX Reference Platform CY2022.
# https://github.com/PixarAnimationStudios/OpenUSD/commit/0e4d360be28e766ed363ac48c7bf28ab7ffa3cf5
if(PXR_VERSION GREATER_EQUAL "2311")
	set(LIB_CXX_STANDARD 17)
	message(STATUS "USD 23.11+ detected - Setting C++17 language standard.")
endif()

set_target_properties(${LIB_TARGET} PROPERTIES
	CXX_STANDARD ${LIB_CXX_STANDARD}
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF)

# This is used by the PLUG_THIS_PLUGIN macro. Must match name in plugInfo.json.
target_compile_definitions(${LIB_TARGET}
	PRIVATE "MFB_PACKAGE_NAME=hdNSI")

set_target_properties(${LIB_TARGET} PROPERTIES
	# The convention seems to be for Hydra plugins to not have the 'lib'
	# prefix. It really just needs to match the name in plugInfo.json
	PREFIX "")

if(NOT APPLE)
	# Don't need to export anything when not on macOS.
	set_target_properties(${LIB_TARGET} PROPERTIES CXX_VISIBILITY_PRESET hidden)
endif()

target_link_libraries(${LIB_TARGET} 3Delight::3DelightAPI)

target_link_libraries(${LIB_TARGET}
	arch cameraUtil plug tf vt gf js work hf hd hdx usdLux usdRender ndr sdf trace pxOsd)

# This should probably be in USD's interface.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(${LIB_TARGET} PRIVATE -Wno-deprecated)
endif()

# Workaround for https://github.com/PixarAnimationStudios/USD/issues/1279
if (MSVC_VERSION GREATER_EQUAL 1930)
	target_compile_options(${LIB_TARGET} PRIVATE "/Zc:inline-")
endif()

install(TARGETS ${LIB_TARGET}
	DESTINATION "${HYDRANSI_INSTALL_PREFIX}hdNSI")

# plugInfo.json
set(PLUG_INFO_LIBRARY_PATH "hdNSI${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(PLUG_INFO_RESOURCE_PATH "resources")
set(PLUG_INFO_ROOT "..")
configure_file(plugInfo.json plugInfo.json @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json"
	DESTINATION "${HYDRANSI_INSTALL_PREFIX}hdNSI/resources")

# Shaders
add_subdirectory(osl)
