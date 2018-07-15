# HydraNSI (hdNSI)

This repository contains a render delegate for Hydra using the NSI technology.

## Background

**Hydra**
Pixar USD (Universal Scene Description) introduced a model/API called Hydra (allegedly named after the multi-headed monster in Greek mythology). Hydra allows different sources of graphic data (indeed called "heads") to produce images of a 3D scene via rendering backends which receive live data updates (scene edits).

> https://github.com/PixarAnimationStudios/USD

**NSI**
NSI (Nodal Scene Interface) is a simple and flexible scene description API which is purposely designed to communicate with a render engine. It is used by the new 3Delight NSI renderer together with OSL (Open Shading Language) for shading. One of the goals of NSI is interactive rendering of editable scene descriptions: this feature, as well as many other, make NSI a perfect candidate for a Hydra rendering backend which we call HydraNSI (*hdNSI*). 

> https://gitlab.com/3DelightOpenSource/HydraNSI/blob/master/nsi.pdf)

HydraNSI can be easily compiled as a plug-in part of the USD toolset. It can naturally be used by any current and future application that implements the USD API as a data model, and which uses Hydra for visualisation. As a practical example, it can be added to the USD viewing utility **usdview**.


## Building
> This code was tested with Pixar USD version 0.8.5a and 3Delight NSI v13.4.9

Once you are able to build Pixar USD, building HydraNSI is very easy:


1. Add the hdNSI folder to your USD distribution, the injection point being:
  https://github.com/PixarAnimationStudios/USD/tree/master/pxr/imaging/plugin
  
2. Make sure to use a CMAKE set to build *hdNSI*:

| CMAKE Variable       | Value     |
| -------------------- | --------- |
| PXR_BUILD_NSI_PLUGIN | 1         |

  
3. Obtain a copy of 3Delight | NSI contaiing the rendering library, include headers and OSL shaders, then set the following CMAKE variables:

| CMAKE Variable  | Value                       |         |                                                         |
| --------------- | --------------------------- | ------- | ------------------------------------------------------- |
| NSI_INCLUDE_DIR | /path/to/nsi-include-folder | Linux   | /usr/local/3delight-version/Linux_64/include            |
|                 |                             | macOS   | /Applications/3Delight/include                          |
|                 |                             | Windows | C:\Program Files\3Delight\include                       |
| NSI_LIBRARY     | /path/to/nsi-library-file   | Linux   | /usr/local/3delight-version/Linux_64/lib/lib3delight.so |
|                 |                             | macOS   | /Applications/3Delight/lib/lib3delight.dylib            |
|                 |                             | Windows | C:\Program Files\3Delight\lib                           |

## Testing

Currently HydraNSI supports the following:

- Geometric primitives:
  - polygon mesh
  - subdivision surfaces
  - points (particles))
  - curves (hair/fur)
- instancing of primitives
- cameras
- shading: currently meshes and points are shaded using dl3DelightMaterial.oso which defaults to a mostly diffusive material (oren-nayar) with a degree of glossy refelection (ggx).
- lighting: currently everything is lit by a directional light shining light from the camera pov (note that in NSI directional lights are actual environment lights, and with the default angle of 360 degrees this light behaves is like a uniform white light environment).

**How to test**

From an environment where both `usdview` and the NSI command-line renderer `renderdl` can be executed:

- On your terminal, launch `usdview`, e.g: *usdview /path/to/file.usd*
- Switch to View> Hydra Renderer: “NSI”
- Try loading some USD files, e.g:
  - Kitchen
  - Instanced city
  - Apple USDZ examples (which can be unzipped to access the usd files)
- Explore the source code and issues in this repository & contribute


> Contributors may contact us at support@3delight.com to obtain a copy of 3Delight NSI for development purposes.


## Future

We are looking for community contributions to implement the following:


- add **usdShade** schema support for 3Delight OSL materials:
  - [https://graphics.pixar.com/usd/docs/UsdShade-Material-Assignment.html](https://graphics.pixar.com/usd/docs/UsdShade-Material-Assignment.html)
  - https://graphics.pixar.com/usd/docs/api/usd_shade_page_front.html
- add **usdLux** schema support for 3delight OSL lights:
  -  [https://graphics.pixar.com/usd/docs/api/usd_lux_page_front.html](https://graphics.pixar.com/usd/docs/api/usd_lux_page_front.html)
- add upcoming **usdVolume** schema support for 3Delight OpenVDB volumes:
  - based on Side Effects work-in-progress contributions to USD
- add OSL matching shaders for **UsdPreviewSurface**:
  - https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html



## Feedback & Contributions

Feel free to log issues or submit pull requests on this repository. 

If you need to get in touch with us e-mail support@3delight.com


## Credits

This work was authored by:

**J Cube Inc.** — Marco Pantaleoni, Bo Zhou, Paolo Berto Durante

Copyright © 2018 Illumination Research Ptv Ltd.


## License

Apache 2.0
