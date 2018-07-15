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
#ifndef HDNSI_RENDER_PARAM_H
#define HDNSI_RENDER_PARAM_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"

#include <atomic>
#include <nsi.h>

PXR_NAMESPACE_OPEN_SCOPE

///
/// \class HdNSIRenderParam
///
/// The render delegate can create an object of type HdRenderParam, to pass
/// to each prim during Sync(). HdNSI uses this class to pass top-level
/// NSI state around.
/// 
class HdNSIRenderParam final : public HdRenderParam {
public:
    HdNSIRenderParam(NSIContext_t ctx)
        : _ctx(ctx), _sceneVersion(0)
        {}
    virtual ~HdNSIRenderParam() = default;

    /// Accessor for the top-level NSI scene.
    NSIContext_t AcquireSceneForEdit() {
        _sceneVersion++;
        return _ctx;
    }
    /// Accessor for the top-level NSI device (library handle).
    NSIContext_t GetNSIContext() { return _ctx; }

    /// Return the scene "version", a counter indicating how many edits have
    /// been made to the scene.  Render passes can use this to determine whether
    /// they have stale sample data.
    int GetSceneVersion() { return _sceneVersion; }

private:
    /// A handle to the top-level NSI context/scene.
    NSIContext_t _ctx;
    /// A numerical "version" of the scene: how many edits have been made.
    std::atomic<int> _sceneVersion;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDNSI_RENDER_PARAM_H
