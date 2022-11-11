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

# Using alternate target name should not change library name.
# Alghouth I think that would be ok as long as plugInfo.json matches.
set_target_properties(${LIB_TARGET} PROPERTIES OUTPUT_NAME hdNSI)

# c++14 is needed on my linux system to build with USD includes.
# I'm not certain what the actual requirement is for USD.
set_target_properties(${LIB_TARGET} PROPERTIES
	CXX_STANDARD 14
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
	arch cameraUtil plug tf vt gf work hf hd hdx usdLux usdRender ndr sdf trace pxOsd)

# This should probably be in USD's interface.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(${LIB_TARGET} PRIVATE -Wno-deprecated)
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
