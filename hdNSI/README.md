HydraNSI (hdNSI)
================

This repository contains a render delegate for Hydra using the NSI technology.


Background
----------

**Hydra**

Pixar USD (Universal Scene Description) introduced a model/API called Hydra (allegendly named after the mithological multi-headed greek monster). Hydra allows different sources of graphic data (indeed called "heads") to produce images of a 3D scene via rendering backends which receive live data updates (scene edits). 

  Ref: link to Pixar USD

**NSI**
Illumination Research NSI (Nodal Scene Interface) is a flexible, modern API for renderers. It is used by the 3Delight NSI renderer together with OSL (Open Shading Language) for shading. One of the goals of NSI is interactive rendering of editable scene descriptions.

  Ref: link to NSI PDF

This natural overalp makes NSI a perfect candidate for a Hydra rendering backend which we call HydraNSI (hdNSI). Furthermore this project serves as an example for another design goal f

HydraNSI can be easily compiled as a plug-in part of the USD toolset. It can be naturally used by any current and future application that implements the USD API as a data model, and which uses Hydra for vizualization. For example it can be added to the USD viewing utility "usdview".


<screenshot>


Building
--------

Once you are able to build Pixar USD, building HydraNSI is very easy:

- simply add the hdNSI folder to your USD distribution
- make sure the CMAKE file is set to build hdNSI
- obtain a copy of the NSI rendering library and include headers 

Testing
-------

- Launch usdview
- Switch to View> Hydra Renderer: NSI 
- explore the code & contribute


Future & TODO
-------------

We are looking for collaborators to implement the following:

- add usdShade schema support for 3Delight OSL materials (https://graphics.pixar.com/usd/docs/UsdShade-Material-Assignment.html and https://graphics.pixar.com/usd/docs/api/usd_shade_page_front.html)
- add usdLux schema support for 3delight OSL lights (https://graphics.pixar.com/usd/docs/api/usd_lux_page_front.html)
- add usdVolume scheme support for 3Delight OpenVDB volumes (based on Side Effects work-in-progress contributions to USD).
- add OSL shaders for UsdPreviewSurface (https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html)


Credits
-------

This work was authored by Marco Pantaleoni, Bo Zhou and Paolo Berto Durante at J Cube Inc.

(c) 2018 Illumination Research Ptv Ltd


Feedback & Contributions
------------------------

Feel free to log issues or submit pull requests on this repopsitory. If you need to get in touch with us e-mail support@3delight.com


