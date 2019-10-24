set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdNSI)

add_subdirectory(osl)

pxr_plugin(hdNSI
   LIBRARIES
        arch
        plug
        tf
        vt
        gf
        work
        hf
        hd
        hdx
        usdLux
        ${TBB_LIBRARIES}

    INCLUDE_DIRS
        ${TBB_INCLUDE_DIRS}
        ${NSI_INCLUDE_DIR}

    RESOURCE_FILES
        plugInfo.json
        ${OSL_RESOURCES}

    PRIVATE_HEADERS
        renderParam.h

    PRIVATE_CLASSES
        curves
        instancer
        light
        material
        materialAssign
        mesh
        pointcloud
        primvars
        outputDriver
        renderBuffer
        renderDelegate
        rendererPlugin
        renderPass
        rprimBase
        tokens

    DISABLE_PRECOMPILED_HEADERS
)
