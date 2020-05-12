# Find pxr (Pixar's USD)
find_package(pxr CONFIG)

if(NOT pxr_FOUND)
	# Try to find USD as part of Houdini.
	find_package(HoudiniUSD)
endif()

if(NOT pxr_FOUND AND NOT HoudiniUSD_FOUND)
	message(FATAL_ERROR "Required: USD install or Houdini with included USD.")
endif()

find_package(3Delight REQUIRED)

add_library(
	hdNSI SHARED

	camera.cpp
	curves.cpp
	discoveryPlugin.cpp
	field.cpp
	instancer.cpp
	light.cpp
	materialAssign.cpp
	material.cpp
	mesh.cpp
	osoParserPlugin.cpp
	outputDriver.cpp
	pointcloud.cpp
	primvars.cpp
	renderBuffer.cpp
	renderDelegate.cpp
	rendererPlugin.cpp
	renderPass.cpp
	rprimBase.cpp
	tokens.cpp
	volume.cpp
	)

# c++14 is needed on my linux system to build with USD includes.
# I'm not certain what the actual requirement is for USD.
set_target_properties(hdNSI PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF)

# This is used by the PLUG_THIS_PLUGIN macro. Must match name in plugInfo.json.
target_compile_definitions(hdNSI
	PRIVATE "MFB_PACKAGE_NAME=hdNSI")

set_target_properties(hdNSI PROPERTIES
	# The convention seems to be for Hydra plugins to not have the 'lib'
	# prefix. It really just needs to match the name in plugInfo.json
	PREFIX ""
	# Don't need to export anything.
	CXX_VISIBILITY_PRESET hidden)

target_link_libraries(hdNSI 3Delight::3DelightAPI)

target_link_libraries(hdNSI
	arch cameraUtil plug tf vt gf work hf hd hdx usdLux usdRender ndr sdf trace pxOsd)

# This should probably be in USD's interface.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(hdNSI PRIVATE -Wno-deprecated)
endif()

install(TARGETS hdNSI
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
