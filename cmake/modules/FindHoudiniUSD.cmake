# This finds Pixar USD which is part of a Houdini installation.

set(HUSD_REQ_VARS "")

find_path(Houdini_USD_INCLUDE_DIR
	"pxr/pxr.h"
	PATHS ENV HFS
	PATH_SUFFIXES "toolkit/include"
	NO_DEFAULT_PATH)
list(APPEND HUSD_REQ_VARS "Houdini_USD_INCLUDE_DIR")

find_path(Houdini_USD_LIB_DIR
	"libpxr_tf${CMAKE_SHARED_LIBRARY_SUFFIX}"
	PATHS ENV HFS
	PATH_SUFFIXES
		"dsolib" # Linux and Windows
		"../Libraries" # macOS
		"bin" # Windows (dll)
	NO_DEFAULT_PATH)
list(APPEND HUSD_REQ_VARS "Houdini_USD_LIB_DIR")

if(WIN32)
	find_path(Houdini_USD_IMPLIB_DIR
		"libpxr_tf.lib"
		PATHS ENV HFS
		PATH_SUFFIXES "custom/houdini/dsolib" # Windows (import lib)
		NO_DEFAULT_PATH)
	list(APPEND HUSD_REQ_VARS "Houdini_USD_IMPLIB_DIR")
endif()

find_path(Houdini_Python_INCLUDE_DIR
	"pyconfig.h"
	PATHS ENV HFS
	PATH_SUFFIXES
		"toolkit/include/python2.7"
		"toolkit/include/python3.7"
		"toolkit/include/python3.7m"
		"toolkit/include/python3.9" # Houdini 19.5
		"toolkit/include/python3.10" # Houdini 20.0
	NO_DEFAULT_PATH)
list(APPEND HUSD_REQ_VARS "Houdini_Python_INCLUDE_DIR")

find_file(
	Houdini_Python_LIB
	NAMES
		"libpython2.7${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix
		"libpython3.7m${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix
		"libpython3.9${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix, Houdini 19.5
		"libpython3.10${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix, Houdini 20.0
		#"python27.dll" "python37.dll" "python39.dll" # Windows
		"python27.lib" "python37.lib" "python39.lib python310.lib" # Windows (import lib)
	PATHS ENV HFS
	PATH_SUFFIXES
		"python/lib" # Linux
		"../../../../Python.framework/Versions/Current/lib" # macOS
		#"bin" # Windows
		"python27/libs" "python37/libs" "python39/libs python310/libs" # Windows (import lib)
	NO_DEFAULT_PATH)
list(APPEND HUSD_REQ_VARS "Houdini_Python_LIB")

find_file(
	Houdini_Boostpython_LIB
	"libhboost_python-mt${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix
	"libhboost_python27${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix, Houdini > 18.0
	"libhboost_python37${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix, Houdini > 18.5
	"libhboost_python39${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix, Houdini 19.5
	"libhboost_python310${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix, Houdini 20.0
	"hboost_python-mt.lib" # Windows (import lib)
	"hboost_python27-mt-x64.lib" # Windows (import lib), Houdini > 18.0
	"hboost_python37-mt-x64.lib" # Windows (import lib), Houdini > 18.5
	"hboost_python39-mt-x64.lib" # Windows (import lib), Houdini 19.5
	"hboost_python310-mt-x64.lib" # Windows (import lib), Houdini 20.0
	PATHS ENV HFS
	PATH_SUFFIXES
		"dsolib" # Linux
		"../Libraries" # macOS
		#"bin" # Windows
		"custom/houdini/dsolib" # Windows (import lib)
	NO_DEFAULT_PATH)
list(APPEND HUSD_REQ_VARS "Houdini_Boostpython_LIB")

if(EXISTS "${Houdini_USD_INCLUDE_DIR}/pxr/pxr.h")
	file(READ "${Houdini_USD_INCLUDE_DIR}/pxr/pxr.h" PXR_INCLUDE_CONTENTS)
	string(REGEX MATCH "#define PXR_VERSION [0-9]+" version_define "${PXR_INCLUDE_CONTENTS}")
	string(REGEX MATCH "[0-9]+" PXR_VERSION "${version_define}")
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
	"HoudiniUSD"
	REQUIRED_VARS ${HUSD_REQ_VARS}
	VERSION_VAR PXR_VERSION)

if(HoudiniUSD_FOUND)
	message(STATUS "  Houdini USD includes: ${Houdini_USD_INCLUDE_DIR}")
	message(STATUS "  Houdini USD libs: ${Houdini_USD_LIB_DIR}")
	message(STATUS "  Houdini Python includes: ${Houdini_Python_INCLUDE_DIR}")
	message(STATUS "  Houdini Python lib: ${Houdini_Python_LIB}")
	message(STATUS "  Houdini Boost.Python lib: ${Houdini_Boostpython_LIB}")
endif()

if(HoudiniUSD_FOUND AND NOT TARGET hd)
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
			IMPORTED_LOCATION "${Houdini_USD_LIB_DIR}/libpxr_${targetName}${CMAKE_SHARED_LIBRARY_SUFFIX}"
			INTERFACE_INCLUDE_DIRECTORIES ${Houdini_USD_INCLUDE_DIR})
		if(MSVC)
			set_target_properties(${targetName} PROPERTIES
				IMPORTED_IMPLIB "${Houdini_USD_IMPLIB_DIR}/libpxr_${targetName}.lib")
		endif()
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			# We need to match the C++ ABI used by Houdini. This changed from
			# old to new in Houdini 20. We test the USD version as a proxy.
			if(PXR_VERSION GREATER_EQUAL "2308")
				target_compile_definitions(${targetName}
					INTERFACE "_GLIBCXX_USE_CXX11_ABI=1")
			else()
				target_compile_definitions(${targetName}
					INTERFACE "_GLIBCXX_USE_CXX11_ABI=0")
			endif()
		endif()
		if(WIN32)
			# Shut up compiler about warnings from USD.
			target_compile_options(${targetName} INTERFACE
				"/wd4506" "/wd4244" "/wd4305" "/wd4267")
			# For automatically linked libraries (python, tbb)
			target_link_directories(${targetName}
				INTERFACE "$ENV{HFS}/custom/houdini/dsolib")
		endif()
		# Suppress TBB "deprecated" warnings from USD
		target_compile_definitions( ${targetName}
			INTERFACE "TBB_SUPPRESS_DEPRECATED_MESSAGES=1" )
	endforeach()
	# Add python to tf, usdImagingGL targets.
	target_include_directories(tf INTERFACE ${Houdini_Python_INCLUDE_DIR})
	target_link_libraries(tf INTERFACE ${Houdini_Python_LIB})
	target_include_directories(usdImagingGL INTERFACE
		${Houdini_Python_INCLUDE_DIR})
	target_link_libraries(usdImagingGL INTERFACE ${Houdini_Python_LIB})
	# Add Boost.Python to tf, vt. Should actually be in many more but that's
	# more than enough to get it linked in for us.
	target_link_libraries(tf INTERFACE ${Houdini_Boostpython_LIB})
	target_compile_definitions(tf INTERFACE HBOOST_ALL_NO_LIB)
	target_link_libraries(vt INTERFACE ${Houdini_Boostpython_LIB})
	target_compile_definitions(vt INTERFACE HBOOST_ALL_NO_LIB)
	# This is only needed on arm, I think because the tbb version is old and
	# uses a generic branch for atomics. Should be removed later.
	if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
		target_link_libraries(tf INTERFACE tbb)
	endif()
endif()
