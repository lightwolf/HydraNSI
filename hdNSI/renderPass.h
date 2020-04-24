//
// Copyright 2016 Pixar
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
#ifndef HDNSI_RENDER_PASS_H
#define HDNSI_RENDER_PASS_H

#include "outputDriver.h"
#include "renderBuffer.h"
#include "renderParam.h"

#include <pxr/pxr.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/sprim.h>
#if defined(PXR_VERSION) && PXR_VERSION <= 2002
#include <pxr/imaging/hdx/compositor.h>
#endif

#include <nsi.hpp>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderDelegate;

/// \class HdNSIRenderPass
///
/// HdRenderPass represents a single render iteration, rendering a view of the
/// scene (the HdRprimCollection) for a specific viewer (the camera/viewport
/// parameters in HdRenderPassState) to the current draw target.
///
/// This class does so by raycasting into the NSI scene.
///
class HdNSIRenderPass final : public HdRenderPass {
public:
    /// Renderpass constructor.
    ///   \param index The render index containing scene data to render.
    ///   \param collection The initial rprim collection for this renderpass.
    HdNSIRenderPass(HdRenderIndex *index,
                    HdRprimCollection const &collection,
                    HdNSIRenderDelegate *renderDelegate,
                    HdNSIRenderParam *renderParam);

    /// Renderpass destructor.
    virtual ~HdNSIRenderPass();

    // -----------------------------------------------------------------------
    // HdRenderPass API

    virtual bool IsConverged() const override;

    void RenderSettingChanged(const TfToken &key);

protected:

    // -----------------------------------------------------------------------
    // HdRenderPass API

    /// Draw the scene with the bound renderpass state.
    ///   \param renderPassState Input parameters (including viewer parameters)
    ///                          for this renderpass.
    ///   \param renderTags Which rendertags should be drawn this pass.
    virtual void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
                          TfTokenVector const &renderTags) override;

    /// Update internal tracking to reflect a dirty collection.
    virtual void _MarkCollectionDirty() override {}

private:

    void _CreateNSIOutputs(const HdRenderPassAovBindingVector &bindings);

    // -----------------------------------------------------------------------
    // Internal API


    // Needed by output system to get correct Z.
    HdNSIOutputDriver::ProjData _depthProj;

    // Handles to all nodes used to define outputs (layers, drivers).
    std::vector<std::string> _outputNodes;

    // AOV bindings for which the above output nodes were created.
    HdRenderPassAovBindingVector _aovBindings;

    // Default render buffers when none are provided.
    HdNSIRenderBuffer _colorBuffer, _depthBuffer;

    // The width of the viewport we're rendering into.
    unsigned int _width;
    // The height of the viewport we're rendering into.
    unsigned int _height;

    // A handle to the render delegate.
    HdNSIRenderDelegate *_renderDelegate;

    // A handle to the render param.
    HdNSIRenderParam *_renderParam;

    // Prefix to all handles used in here.
    std::string _handlesPrefix;

    std::string Handle(const char *suffix) const;

    std::string ScreenHandle() const;
    void SetOversampling() const;

    // Our camera-related handles.
    void _CreateNSICamera();

    std::string _cameraXformHandle;
    std::string _cameraShapeHandle;

    std::string _outputDriverHandle;

    // Our headlight handle.
    std::string ExportNSIHeadLightShader();
    void _CreateNSIHeadLight(bool create);

    std::string _headlightXformHandle;

    // Our environment light handles.
    void _CreateNSIEnvironmentLight(bool create);

    std::string _envlightXformHandle;

    // Update the perspective camera parameters.
    void _UpdateNSICamera();

    // Status of the 3Delight renderer.
    enum RenderStatus {
        Stopped,
        Running
    };
    RenderStatus _renderStatus;

    // The view matrix
    GfMatrix4d _viewMatrix;
    GfMatrix4d _projMatrix;

    // Compositor to copy pixels to viewport.
#if defined(PXR_VERSION) && PXR_VERSION <= 2002
    HdxCompositor _compositor;
#endif
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDNSI_RENDER_PASS_H
// vim: set softtabstop=4 expandtab shiftwidth=4:
