# This finds Pixar USD which is part of a Katana installation.
# Requires KATANA_HOME to be the path to that installation.

set(KATUSD_REQ_VARS "")

find_path(Katana_USD_INCLUDE_DIR
	"pxr/pxr.h"
	HINTS "${KATANA_HOME}"
	PATH_SUFFIXES "external/FnUSD/include"
	NO_CACHE NO_DEFAULT_PATH)
list(APPEND KATUSD_REQ_VARS "Katana_USD_INCLUDE_DIR")

find_path(Katana_USD_LIB_DIR
	"${CMAKE_SHARED_LIBRARY_PREFIX}fntf${CMAKE_SHARED_LIBRARY_SUFFIX}"
	HINTS "${KATANA_HOME}"
	PATH_SUFFIXES "bin"
	NO_CACHE NO_DEFAULT_PATH)
list(APPEND KATUSD_REQ_VARS "Katana_USD_LIB_DIR")

if(WIN32)
	find_path(Katana_USD_IMPLIB_DIR
		"fntf.lib"
		HINTS "${KATANA_HOME}"
		PATH_SUFFIXES "bin"
		NO_CACHE NO_DEFAULT_PATH)
	list(APPEND KATUSD_REQ_VARS "Katana_USD_IMPLIB_DIR")

	file(GLOB WINBOOSTPATH RELATIVE "${KATANA_HOME}"
		"${KATANA_HOME}/external/foundryboost/include/boost-*")
endif()

find_path(Katana_Boost_INCLUDE_DIR
	"boost/version.hpp"
	HINTS "${KATANA_HOME}"
	PATH_SUFFIXES
		"external/foundryboost/include" # Linux
		${WINBOOSTPATH} # Windows, which ends with eg. boost-1_70
	NO_CACHE NO_DEFAULT_PATH)
list(APPEND KATUSD_REQ_VARS "Katana_Boost_INCLUDE_DIR")

find_path(Katana_Python_INCLUDE_DIR
	"pyconfig.h"
	HINTS "${KATANA_HOME}"
	PATH_SUFFIXES
		"bin/python2.7/include/python2.7" # Linux
		"bin/python3.7/include/python3.7m" # Linux
		"bin/python3.9/include/python3.9" # Linux
		"bin/python3.10/include/python3.10" # Linux
		"bin/include/include" # Windows being weird as usual
	NO_CACHE NO_DEFAULT_PATH)
list(APPEND KATUSD_REQ_VARS "Katana_Python_INCLUDE_DIR")

find_file(
	Katana_Python_LIB
	NAMES
		"libpython2.7${CMAKE_SHARED_LIBRARY_SUFFIX}" # Linux
		"libpython3.7m${CMAKE_SHARED_LIBRARY_SUFFIX}" # Linux
		"libpython3.9${CMAKE_SHARED_LIBRARY_SUFFIX}" # Linux
		"libpython3.10${CMAKE_SHARED_LIBRARY_SUFFIX}" # Linux
		"python27.lib" # Windows (import lib)
		"python37.lib" # Windows (import lib)
		"python39.lib" # Windows (import lib)
		"python310.lib" # Windows (import lib)
	HINTS "${KATANA_HOME}"
	PATH_SUFFIXES
		"bin/python2.7/lib" # Linux
		"bin/python3.7/lib" # Linux
		"bin/python3.9/lib" # Linux
		"bin/python3.10/lib" # Linux
		"bin" # Windows (import lib)
	NO_CACHE NO_DEFAULT_PATH)
list(APPEND KATUSD_REQ_VARS "Katana_Python_LIB")

find_path(Katana_tbb_INCLUDE_DIR
	"tbb/atomic.h"
	HINTS "${KATANA_HOME}"
	PATH_SUFFIXES "external/tbb/include"
	NO_CACHE NO_DEFAULT_PATH)
list(APPEND KATUSD_REQ_VARS "Katana_tbb_INCLUDE_DIR")

if(EXISTS "${Katana_USD_INCLUDE_DIR}/pxr/pxr.h")
	file(READ "${Katana_USD_INCLUDE_DIR}/pxr/pxr.h" PXR_INCLUDE_CONTENTS)
	string(REGEX MATCH "#define PXR_VERSION [0-9]+" version_define "${PXR_INCLUDE_CONTENTS}")
	string(REGEX MATCH "[0-9]+" PXR_VERSION "${version_define}")
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
	"KatanaUSD"
	REQUIRED_VARS ${KATUSD_REQ_VARS}
	VERSION_VAR PXR_VERSION)

if(KatanaUSD_FOUND)
	message(STATUS "  Katana USD includes: ${Katana_USD_INCLUDE_DIR}")
	message(STATUS "  Katana USD libs: ${Katana_USD_LIB_DIR}")
	message(STATUS "  Katana boost includes: ${Katana_Boost_INCLUDE_DIR}")
	message(STATUS "  Katana Python includes: ${Katana_Python_INCLUDE_DIR}")
	message(STATUS "  Katana Python lib: ${Katana_Python_LIB}")
	message(STATUS "  Katana TBB includes: ${Katana_tbb_INCLUDE_DIR}")
endif()

if(KatanaUSD_FOUND AND NOT TARGET hd)
	# Generic creation of the usd targets. This is not meant to be perfect. The
	# criteria is "does it work for our use". Also, these names match the ones
	# of the USD distribution so we can use either without many conditions.
	# We're missing the interlib dependencies here so the plugin has to link a
	# bit more stuff explicitly.
	foreach(targetName
		arch tf gf js trace work plug vt ar kind sdf ndr sdr pcp usd usdGeom
		usdVol usdLux usdMedia usdShade usdRender usdHydra usdRi usdSkel usdUI
		usdUtils garch hf hio cameraUtil pxOsd glf hgi hgiGL hd hdSt hdx
		usdImaging usdImagingGL usdRiImaging usdSkelImaging usdVolImaging
		usdAppUtils usdviewq)
		add_library(${targetName} SHARED IMPORTED)
		set_target_properties(${targetName} PROPERTIES
			IMPORTED_LOCATION "${Katana_USD_LIB_DIR}/libfn${targetName}${CMAKE_SHARED_LIBRARY_SUFFIX}")
		target_include_directories(${targetName}
			INTERFACE "${Katana_USD_INCLUDE_DIR}"
			INTERFACE "${Katana_Boost_INCLUDE_DIR}"
			INTERFACE "${Katana_Python_INCLUDE_DIR}"
			INTERFACE "${Katana_tbb_INCLUDE_DIR}")
		if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
			set_target_properties(${targetName} PROPERTIES
				IMPORTED_IMPLIB "${Katana_USD_IMPLIB_DIR}/fn${targetName}.lib")
			# For automatically linked libraries (eg. boost)
			target_link_directories(${targetName}
				INTERFACE "${KATANA_HOME}/bin")
		endif()
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			# Katana builds with the old ABI. We need to match.
			target_compile_definitions(${targetName}
				INTERFACE "_GLIBCXX_USE_CXX11_ABI=0")
		endif()
		if(MSVC)
			# Shut up compiler about warnings from USD.
			target_compile_options(${targetName} INTERFACE
				"/wd4506" "/wd4244" "/wd4305" "/wd4267")
		endif()
		# Suppress TBB "deprecated" warnings from USD
		target_compile_definitions( ${targetName}
			INTERFACE "TBB_SUPPRESS_DEPRECATED_MESSAGES=1" )
	endforeach()
	# Add python to the usd libs which use it.
	foreach(targetName tf vt ar sdf pcp ar)
		target_include_directories(${targetName}
			INTERFACE ${Katana_Python_INCLUDE_DIR})
		target_link_libraries(${targetName}
			INTERFACE ${Katana_Python_LIB})
	endforeach()
endif()
