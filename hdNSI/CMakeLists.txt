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
        ${TBB_LIBRARIES}

    INCLUDE_DIRS
        ${TBB_INCLUDE_DIRS}
        ${NSI_INCLUDE_DIR}

    PUBLIC_CLASSES
        config
        instancer
        mesh
        pointcloud
        curves
        rendererPlugin
        renderDelegate
        renderPass
        sampler

    PUBLIC_HEADERS
        renderParam.h

    RESOURCE_FILES
        plugInfo.json
)

# if (X11_FOUND OR APPLE)
# pxr_build_test(testHdNSI
#     LIBRARIES
#         hdNSI
#         garch
#         arch
#     CPPFILES
#         testenv/testHdNSI.cpp
# )
# endif()
