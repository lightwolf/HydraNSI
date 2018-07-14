# HydraNSI (hdNSI)

This repository contains a render delegate for Hydra using the NSI technology.

## Background

**Hydra**
Pixar USD (Universal Scene Description) introduced a model/API called Hydra (allegedly named after the multi-headed monster in Greek mythology). Hydra allows different sources of graphic data (indeed called "heads") to produce images of a 3D scene via rendering backends which receive live data updates (scene edits).

> https://github.com/PixarAnimationStudios/USD

**NSI**
NSI (Nodal Scene Interface) is a simple and flexible scene description API which is purposely designed to communicate with a render engine. It is used by the new 3Delight NSI renderer together with OSL (Open Shading Language) for shading. One of the goals of NSI is interactive rendering of editable scene descriptions: this feature, as well as many other, make NSI a perfect candidate for a Hydra rendering backend which we call HydraNSI (hdNSI). 


> http://3delight.com/nsi.pdf


HydraNSI can be easily compiled as a plug-in part of the USD toolset. It can naturally be used by any current and future application that implements the USD API as a data model, and which uses Hydra for visualisation. As a practical example, it can be added to the USD viewing utility `usdview`.
