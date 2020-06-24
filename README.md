# HydraNSI (hdNSI)

This repository contains a render delegate for Hydra using the NSI technology.

![](/uploads/8bc6d08ad137bea790682be25bd9f5f3/nsi_kitchen.png)

![](/uploads/d9f5310cc88d6adddeffa4238ecdb9ed/NSI_Solaris.jpg)

| Kitchen | Hair | Points |
| -------- | -------- | -------- | 
| ![](/uploads/c9e2fa6aa4f3cf0f559e6c71efd0b5e8/nsi_kitchen_detail.png) | ![](/uploads/53a9d2d71039d834649beeff557c1fb1/nsi_1M_hair.png)   | ![](/uploads/444a58398fd19d55e2dffd0520736e23/pointcloud-color-size.png)  |


![](https://assets.gitlab-static.net/uploads/-/system/project/avatar/7466299/hydra_gitlab_128px.png)


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


## Integration

hdNSI is used in the following products:

- [Multiverse | USD](https://multi-verse.io)
- [Solaris](https://sidefx.com)
- [Katana](https://foundry.com) (Experimental)

## Building
> This code supports Pixar USD version **19.11** or later.
> The minimum 3Delight NSI version needed is **2.0.2**.

Building HydraNSI should be straightforward. Make sure the DELIGHT environment variable points to your 3Delight installation. Then provide USD in one of two ways:

1. An installation of USD. Define pxr_DIR to point to it when running cmake, if required.
2. The USD which is provided with Houdini. The HFS environment variable should point to the Houdini installation.

## Missing Features

- UsdSkel
- Motion Blur
- Some Object and Light attributes

## AOVS
This is a list of what can go in the RenderVar nodes. The first value is what
goes into "Source Name". The second is the "Data Type", when not the default of
"color3f". Note that Houdini currently has trouble processing "vector3f" so you
should actually leave those to "color3f" until this gets fixed.

The "Format" field should be set to a float or half type which matches data
type (eg. "float3" or "half3" for a color3f). This chooses if you end up with a
16-bit or 32-bit exr file.

For the beauty (and likely other AOVs but there's little point), using float4
data type instead of color3f will give you an alpha channel. Likewise, use
half4/float4 format instead of half3/float3.

The houdini scene contains some mixed examples of all this.

- Ci
- diffuse
- subsurface
- reflection
- refraction
- volume
- incandescence
- toon_base
- toon_diffuse
- toon_specular
- outlines float4
- builtin:z float
- builtin:P.camera vector3f
- builtin:N.camera vector3f
- builtin:P.world vector3f
- builtin:N.world vector3f
- relighting_multiplier
- relighting_reference

## Testing

From an environment where both `usdview` and the NSI command-line renderer `renderdl` can be executed:

- In your terminal, add the plugin to USD's search path when launching `usdview`:
  
    ```bash
    env PXR_PLUGINPATH_NAME=builddir/output/hdNSI/resources usdview /path/to/file.usd
    ```
    Where builddir is your cmake build directory for HydraNSI. Adjust if your changed the default install location.
- Switch to View> Hydra Renderer: “3Delight”.
- Try loading some USD or ABC files, e.g:
  - [Pixar Kitchen](http://graphics.pixar.com/usd/files/Kitchen_set.zip)
  - [Pixar Instanced city](http://graphics.pixar.com/usd/files/PointInstancedMedCity.zip)
  - [Apple USDZ examples](https://developer.apple.com/arkit/gallery)
  - [J Cube Esper Room](https://j-cube.jp/solutions/multiverse/assets/)
  - [J Cube Maneki](https://j-cube.jp/solutions/multiverse/assets/)
  - [Sidefx Bar](https://www.sidefx.com/contentlibrary/bar-scene/)
  - [nVidia Attic](https://developer.nvidia.com/usd)
  
### Contributors

- Initial working implementation, documentation, videos and testing by [J Cube Inc](https://j-cube.jp)
- Mark Tucker (Side Effects Inc)
- Chris Rydalch
- The initial implementation was based on Pixar's [hdEmbree](https://github.com/PixarAnimationStudios/USD/tree/master/pxr/imaging/plugin/hdEmbree)

Ongoing development by Illumination Research Pte Ltd. (www.3delight.com)

## License

Apache 2.0 License

## Copyright
Copyright 2020 Illumination Research Pte Ltd.
