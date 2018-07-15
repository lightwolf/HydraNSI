//
// Copyright 2017 Pixar
// Copyright 2018 Illumination Research Pte Ltd.
// Authors: J Cube Inc (Marco Pantaleoni, Bo Zhou, Paolo Berto Durante)
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/imaging/hdNSI/rendererPlugin.h"

#include "pxr/imaging/hdx/rendererPluginRegistry.h"
#include "pxr/imaging/hdNSI/renderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the NSI plugin with the renderer plugin system.
TF_REGISTRY_FUNCTION(TfType)
{
    HdxRendererPluginRegistry::Define<HdNSIRendererPlugin>();
}

HdRenderDelegate*
HdNSIRendererPlugin::CreateRenderDelegate()
{
    return new HdNSIRenderDelegate();
}

void
HdNSIRendererPlugin::DeleteRenderDelegate(HdRenderDelegate *renderDelegate)
{
    delete renderDelegate;
}

bool 
HdNSIRendererPlugin::IsSupported() const
{
    // Nothing more to check for now, we assume if the plugin loads correctly
    // it is supported.
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
