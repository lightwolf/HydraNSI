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

    PUBLIC_CLASSES
        instancer
        mesh
        pointcloud
        curves
        rendererPlugin
        renderDelegate
        renderPass
        sampler
        tokens

    PUBLIC_HEADERS
        renderParam.h

    RESOURCE_FILES
        plugInfo.json
        ${OSL_RESOURCES}

    PRIVATE_CLASSES
        light
        material
        materialAssign
        primvars
        outputDriver
        renderBuffer
        rprimBase

    DISABLE_PRECOMPILED_HEADERS
)
