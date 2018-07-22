# HydraNSI Change Log

## v0.3 — July 22, 2018

- Lifted minimum 3Delight NSI version needed for HydraNSI to v13.4.15 which is using the new (and first) dynamic rendering API.
- Now using the new NSI::DynamicAPI: everything in HydraNSI is now updated to use the shared NSI::Context. This will facilitate building 3rd-party apps that use the HydraNSI delegate simply by including the NSI header file (1 file, nsi.h) and linking dynamically to the NSI library. As a corollary, this would allow to write other Hydra "heads" in NSI. 


## v0.2 — July 19, 2018

- Support for orientation (winding order): now most USD and ABC files  will have correct orientation when rendering.
- Complete support for subdivision surfaces (catmull-clark) and related attributes (corner/crease vertices & sharpness).
- Re-factored code for curves and shader assigned to curves.
- Added support for displayColor on points.
- Added support for displayColor on curves.
- Added support for point widths.
- Added support for curve points  widths.
- Points now are shaded with a higher reflect roughness.
- Added support for visibility changes from usdview gui.
- Hierarchical visibility works as well as selective visibility at the same level.
- Removed some useless code and comments (which came from copy & paste of hdEmree).
- Handle curves with less than four control points as "linearcurves" in NSI. Otherwise they are cubic.
- Fixed a coding error in HdNSIRendePass.
- Improved shader construction.
- Re-factored hdNSIRenderPass so that each instance has its display handle.
- Added CHANGELOG.md


## v0.1 — July 15, 2018

This is a USD Hydra plug-in implementing a delegate for the NSI (Nodal Scene Interface) rendering API of the 3Delight | NSI renderer. See README.md for a description of this project.

- geometry primitive support: polygons, subdivisions, points, curves
- instancing of geometric primitives
- camera support
- built-in lighting (confiugurable by env vars)
- built-in shading (depending on geonetry)
- basic rendering config setting (pixel samples and shading samples)

