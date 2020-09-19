# This will put the compiled shaders in an osl subdirectory of resources.

set(OSL_SOURCES
	NoLightingSurface.osl
	UsdLuxLight.osl
	UsdPreviewSurface.osl
	UsdPrimvarReader_float.osl
	UsdPrimvarReader_float2.osl
	UsdPrimvarReader_float3.osl
	UsdPrimvarReader_int.osl
	UsdPrimvarReader_normal.osl
	UsdPrimvarReader_point.osl
	UsdPrimvarReader_string.osl
	UsdPrimvarReader_vector.osl
	UsdUVTexture.osl
	)

foreach(shader ${OSL_SOURCES})
	get_filename_component(shader_name "${shader}" NAME_WE)
	get_filename_component(shader_source "${shader}" ABSOLUTE)

	add_custom_command(
		OUTPUT "${shader_name}.oso"
		COMMAND 3Delight::oslc -o "${shader_name}.oso" -q "${shader_source}"
		DEPENDS "${shader_source}"
		VERBATIM)

	add_custom_target(${shader_name}
		ALL
		DEPENDS "${shader_name}.oso")

	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${shader_name}.oso"
		DESTINATION "${HYDRANSI_INSTALL_PREFIX}hdNSI/resources/osl")
endforeach()
