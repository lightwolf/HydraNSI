set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdNSI)

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

    PRIVATE_CLASSES
        light
        outputDriver

    DISABLE_PRECOMPILED_HEADERS
)
