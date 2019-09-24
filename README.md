# HydraNSI (hdNSI)

This repository contains a render delegate for Hydra using the NSI technology.

![](/uploads/8bc6d08ad137bea790682be25bd9f5f3/nsi_kitchen.png)

![](/uploads/d9f5310cc88d6adddeffa4238ecdb9ed/NSI_Solaris.jpg)

| Kitchen | Hair | Points |
| -------- | -------- | -------- | 
| ![](/uploads/c9e2fa6aa4f3cf0f559e6c71efd0b5e8/nsi_kitchen_detail.png) | ![](/uploads/53a9d2d71039d834649beeff557c1fb1/nsi_1M_hair.png)   | ![](/uploads/444a58398fd19d55e2dffd0520736e23/pointcloud-color-size.png)  |


![](https://assets.gitlab-static.net/uploads/-/system/project/avatar/7466299/nsi_logo_round.png)


## Background

**Hydra** — Pixar USD (Universal Scene Description) introduced a model/API called Hydra (allegedly named after the multi-headed monster in Greek mythology). Hydra allows different sources of graphic data (indeed called "heads") to produce images of a 3D scene via rendering delegates/backends which receive live data updates (scene edits).

> https://github.com/PixarAnimationStudios/USD

**NSI** — The Nodal Scene Interface (NSI) is a simple and flexible scene description API which is purposely designed to communicate with a render engine. It is used by the new 3Delight NSI renderer together with OSL (Open Shading Language) for shading. One of the goals of NSI is interactive rendering of editable scene descriptions: this feature, as well as many other, make NSI a perfect candidate for a Hydra rendering backend which we call HydraNSI (*hdNSI*). 

> https://3delight.com/download (The nsi.pdf specs are located in the doc folder)

**HydraNSI** — Production-quality preview rendering of USD assets, in a Hydra-based viewer, is an appealing idea that allows for a centralised and standardised rendering application. HydraNSI is a Hydra plug-in that converts USD to NSI commands and allows to use 3Delight NSI as a rendering back-end with its rich feature and extensible platform.

HydraNSI can be easily compiled as a plug-in part of the USD toolset. It can naturally be used by any current and future application that implements the USD API as a data model, and which uses Hydra for visualization. As a practical example, it can be added to the USD viewing utility *usdview* or in SideFx's *Solaris*.

## Demo Videos

![Demo Video](https://gitlab.com/3DelightOpenSource/HydraNSI/uploads/683abdd0535432b4483ff9135833ebcf/out.mp4)

→ [more videos](https://gitlab.com/3DelightOpenSource/HydraNSI/wikis/Videos)


## Building
> This code was tested with Pixar USD version **19.10**.
> The minimum 3Delight NSI version needed is **1.6.4**.

Once  you are able to build Pixar USD, building HydraNSI is very easy:


1. Add the hdNSI folder to your USD distribution, the injection point being:
  https://github.com/PixarAnimationStudios/USD/tree/master/pxr/imaging/plugin
  
2. Make sure to use a CMAKE set to build *hdNSI*:

| CMAKE Variable       | Value     |
| -------------------- | --------- |
| PXR_BUILD_NSI_PLUGIN | 1         |

  
3. Obtain a copy of 3Delight | NSI containing the rendering library, include headers and OSL shaders, then set the following CMAKE variables:

| CMAKE Variable  | Value                       |         |                                                         |
| --------------- | --------------------------- | ------- | ------------------------------------------------------- |
| NSI_INCLUDE_DIR | /path/to/nsi-include-folder | Linux   | /usr/local/3delight-version/Linux_64/include            |
|                 |                             | macOS   | /Applications/3Delight/include                          |
|                 |                             | Windows | C:\Program Files\3Delight\include                       |
| NSI_LIBRARY     | /path/to/nsi-library-file   | Linux   | /usr/local/3delight-version/Linux_64/lib/lib3delight.so |
|                 |                             | macOS   | /Applications/3Delight/lib/lib3delight.dylib            |
|                 |                             | Windows | C:\Program Files\3Delight\lib                           |

## Features

Currently HydraNSI supports the following:

- Geometric primitives:
  - Polygon mesh
  - Subdivision surfaces (analytic evaluation at rendertime, no pre-tesselation).
  - Points (particles/pointclouds)
  - Curves (hair/fur)
- Instancing of primitives
- Cameras
- Shading (PBR):
  - Polygons (and subdivisions) use a *dl3DelightMaterial* material which defaults to a mostly diffusive material (Oren-Nayar model) with a degree of glossy reflection (GGX model).
  - Points also use a *dl3DelightMaterial* material, with a higher reflection roughness. 
  - Curves are shaded with the *dlHairAndFur* material (Marschner-Chiang-d'Eon model).

  > Note that the shading network for points and curves have a *dlPrimitiveVariable* so to read per-point and per-curve-vertex color.

- Lighting:
  - Head light: a directional light that uses a *directionalLight* emitter which shines from the camera pov. It is always active. Note that in NSI directional lights are actual environment lights: when an angle of 0 degrees is specified they behave directionally. See nsi.pdf for more informations. 
  - Omni environment: this is another directional light that uses another *directionalLight* emitter. It is used when a file texture is *not* set to be used for environment in the configuration (see below). As per above this is an environment light, though when an angle of 360 degrees it behaves like a uniform environment.
  - HDRI environment: this is a small shading network using *uvCoordEnvironment --> file --> dlEnvironmentShape* which allows to optionally use a HDRI file texture for image-based lighting. It can be enabled via environment variable. Use the HDNSI_ENV_LIGHT_IMAGE environment variable pointing at the file location on disk (.tdl, .exr and .hdr formats are accepted). HDRI environments can be created using data such as [high res probes](http://gl.ict.usc.edu/Data/HighResProbes) and then processed as tiled mipmaps using the following command: *tdlmake -envlatl filename.exr filename.tdl.tif*
  - Procedural Sky environment: this is a small shading network using *dlSky --> dlEnvironmentShape* which uses a HDRI procedural sky (Hosek-Wilkie model) for (procedural) image-based lighting. It is enabled by default and can be disabled by setting the HDNSI_ENV_USE_SKY environment variable set to.

## Testing

From an environment where both `usdview` and the NSI command-line renderer `renderdl` can be executed:

- On your terminal, launch `usdview`:
  
    ```bash
    usdview /path/to/file.usd
    ```
- Switch to View> Hydra Renderer: “NSI”.
- Try loading some USD or ABC files, e.g:
  - [Kitchen](http://graphics.pixar.com/usd/files/Kitchen_set.zip)
  - [Instanced city](http://graphics.pixar.com/usd/files/PointInstancedMedCity.zip)
  - [Apple USDZ examples](https://developer.apple.com/arkit/gallery) -- Note that USDZ is basically a zip file with no compression: you can rename the files from .usdz to .zip and unzip them to access the actual .usd files.
- Further explore the source code and issues in this repository & contribute.

> Contributors may contact us at [support@3delight.com](mailto:support@3delight.com) to obtain a copy of 3Delight NSI for development purposes.


## A note about Alembic files and primitive variables

When reading Alembic files in usdview some variables that control the color and width of primitives will not be read even if the attribute naming matches the USD one. The USD Alembic plug-in reader should be improved by the USD team so to handle proper support of such primitive variables, it could additionally recognize typical primitive variables of DCC Apps (for example in Houdini *Cd*, *width* and *pscale* are standard names and if present they could be converted on-the-fly to the USD respective ones). Feel free to push Pixar in improving their Alembic plug-in: [USD issue #569](https://github.com/PixarAnimationStudios/USD/issues/569).  


## Future

We are looking for community contributions to implement the following:

- testing & bugfixing on all platforms
- add experimental **usdVolume** schema support for 3Delight OpenVDB volumes: [Issue #3](https://gitlab.com/3DelightOpenSource/HydraNSI/issues/3)


## Feedback & Contributions

Feel free to log issues or submit your patched as pull requests on this repository. Contributors will be added to the be credits.

If you need to get in touch with us e-mail [support@3delight.com](mailto:support@3delight.com)


## Credits

This work was authored by:

[J Cube Inc](http://j-cube.jp) — Marco Pantaleoni, Bo Zhou, Davide Selmo, Paolo Berto Durante. 

Copyright © 2019 Illumination Research Ptv Ltd.

### Contributors

We wish to thank the following external contributors:

- Mark Tucker (Side Effects Inc)


## License

Apache 2.0 License
