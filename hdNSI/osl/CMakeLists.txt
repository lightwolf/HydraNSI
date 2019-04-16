# This exports the OSL_RESOURCES list for use in the RESOURCE_FILES section of
# the pxr_plugin function. It will put the compiled shaders in an osl
# subdirectory.

set(OSL_SOURCES
	UsdPreviewSurface.osl
	UsdPrimvarReader_float2.osl
	UsdUVTexture.osl
	)

foreach(shader ${OSL_SOURCES})
	get_filename_component(shader_name ${shader} NAME_WE)
	get_filename_component(shader_source ${shader} ABSOLUTE)

	add_custom_command(
		OUTPUT ${shader_name}.oso
		COMMAND oslc -o ${shader_name}.oso -q ${shader_source}
		DEPENDS ${shader_source})

	add_custom_target(${shader_name}
		ALL
		DEPENDS ${shader_name}.oso)

	list(APPEND OSL_RESOURCES
		${CMAKE_CURRENT_BINARY_DIR}/${shader_name}.oso:osl/${shader_name}.oso)
endforeach()

set(OSL_RESOURCES ${OSL_RESOURCES} PARENT_SCOPE)


