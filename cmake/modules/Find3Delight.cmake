# Find3Delight
#
# Finds the 3Delight installation.
#
# It is search for with the DELIGHT environment variable.
# The location may be overriden by setting 3Delight_ROOT_DIR.
#
# Provides targets:
# - 3Delight::3Delight to actually link with the renderer
# - 3Delight::3DelightAPI to use headers only
# - 3Delight::oslc executable for the osl shader compiler

find_path(3Delight_ROOT_DIR
	include/nsi.h
	PATHS ${DELIGHT} ENV DELIGHT
	NO_DEFAULT_PATH)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
	"3Delight"
	REQUIRED_VARS 3Delight_ROOT_DIR)

# Set standard variables.
if(3Delight_FOUND)
	set(3Delight_INCLUDE_DIRS "${3Delight_ROOT_DIR}/include")
	if(MSVC)
		set(3Delight_LIBRARY
			"${3Delight_ROOT_DIR}/bin/3Delight${CMAKE_SHARED_LIBRARY_SUFFIX}")
	else()
		set(3Delight_LIBRARY
			"${3Delight_ROOT_DIR}/lib/lib3delight${CMAKE_SHARED_LIBRARY_SUFFIX}")
	endif()
	set(3Delight_LIBRARIES "${3Delight_LIBRARY}")
	set(3Delight_oslc_EXECUTABLE
		"${3Delight_ROOT_DIR}/bin/oslc${CMAKE_EXECUTABLE_SUFFIX}")
endif()

# Provide targets.
if(3Delight_FOUND AND NOT TARGET 3Delight::3Delight)
	add_library(3Delight::3DelightAPI INTERFACE IMPORTED)
	set_target_properties(3Delight::3DelightAPI PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES ${3Delight_INCLUDE_DIRS})

	add_library(3Delight::3Delight SHARED IMPORTED)
	set_target_properties(3Delight::3Delight PROPERTIES
		IMPORTED_LOCATION ${3Delight_LIBRARY}
		INTERFACE_INCLUDE_DIRECTORIES ${3Delight_INCLUDE_DIRS})

	add_executable(3Delight::oslc IMPORTED)
	set_target_properties(3Delight::oslc PROPERTIES
		IMPORTED_LOCATION ${3Delight_oslc_EXECUTABLE})
endif()

