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


## Integration

hdNSI is used in the following products:

- [Multiverse | USD](https://multi-verse.io)
- [Solari](https://sidefx.com)
- [Katna](https://foundry.com) (Experimental)

## Building
> This code supports Pixar USD version **19.11** or later.
> The minimum 3Delight NSI version needed is **2.0.2**.

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

## Missing Features

- UsdRender
- UsdSkel

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

### Contributors

- Initial working implementation, docuementation, videos and testing by [J CUBE Inc](https://j-cube.jp)
- Mark Tucker (Side Effects Inc)
- Chris Rydalch
- The initial code base is based on Pixar's [hdEmbre](https://github.com/PixarAnimationStudios/USD/tree/master/pxr/imaging/plugin/hdEmbree)

Ongoing development by Illumination Research Pte Ltd. (www.3delight.com)

## License

Apache 2.0 License

## Copyright
Copyright 2020 Illumination Research Pte Ltd.
