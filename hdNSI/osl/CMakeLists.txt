# This exports the OSL_RESOURCES list for use in the RESOURCE_FILES section of
# the pxr_plugin function. It will put the compiled shaders in an osl
# subdirectory.
set(HDNSI_RESOURCES_SUBDIR "plugin/usd/hdNSI/resources")

find_package(Renderman REQUIRED)

set(OSL_SOURCES
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
	get_filename_component(shader_name ${shader} NAME_WE)
	get_filename_component(shader_source ${shader} ABSOLUTE)

	add_custom_command(
		OUTPUT ${shader_name}.oso
		COMMAND ${RENDERMAN_BINARY_DIR}/oslc -o ${shader_name}.oso -q ${shader_source}
		DEPENDS ${shader_source}
		VERBATIM
	)

	add_custom_target(${shader_name}
		ALL
		DEPENDS ${shader_name}.oso
	)

	install(
	    PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${shader_name}.oso
		DESTINATION ${HDNSI_RESOURCES_SUBDIR}/osl
	)
endforeach()

