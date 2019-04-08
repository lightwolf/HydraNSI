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
#include "pxr/imaging/glf/glew.h"

#include "pxr/imaging/hdNSI/renderPass.h"

#include "pxr/imaging/hdNSI/mesh.h"
#include "pxr/imaging/hdNSI/renderDelegate.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/imaging/hdNSI/tokens.h"

#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/renderPassState.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/work/loops.h"

#include <boost/lexical_cast.hpp>

PXR_NAMESPACE_OPEN_SCOPE

HdNSIRenderPass::HdNSIRenderPass(HdRenderIndex *index,
                                 HdRprimCollection const &collection,
                                 HdNSIRenderDelegate *renderDelegate,
                                 HdNSIRenderParam *renderParam)
    : HdRenderPass(index, collection)
    , _imageHandle{new HdNSIOutputDriver::Handle}
    , _width(0)
    , _height(0)
    , _renderDelegate(renderDelegate)
    , _renderParam(renderParam)
    , _renderStatus(Stopped)
    , _viewMatrix(1.0)
    , _projMatrix(1.0)
{
}

HdNSIRenderPass::~HdNSIRenderPass()
{
    // Stop the render.
    std::shared_ptr<NSI::Context> nsi = _renderParam->AcquireSceneForEdit();

    StopRender();
    nsi->RenderControl(NSI::CStringPArg("action", "wait"));

    delete _imageHandle;
}

void HdNSIRenderPass::RenderSettingChanged(const TfToken &key)
{
    if (key == HdNSIRenderSettingsTokens->pixelSamples)
    {
        SetOversampling();
    }
    if (key == HdNSIRenderSettingsTokens->cameraLightIntensity)
    {
        ExportNSIHeadLightShader();
    }
    if (key == HdNSIRenderSettingsTokens->envLightPath ||
        key == HdNSIRenderSettingsTokens->envLightMapping ||
        key == HdNSIRenderSettingsTokens->envLightIntensity ||
        key == HdNSIRenderSettingsTokens->envAsBackground ||
        key == HdNSIRenderSettingsTokens->envUseSky)
    {
        _CreateNSIEnvironmentLight();
    }
}

void
HdNSIRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState,
                          TfTokenVector const &renderTags)
{
    std::shared_ptr<NSI::Context> nsi = _renderParam->GetNSIContext();

    // If the viewport has changed, resize and reset the sample buffer.
    bool resetImage = false;
    GfVec4f vp = renderPassState->GetViewport();
    if (_width != vp[2] || _height != vp[3]) {
        _width = vp[2];
        _height = vp[3];

        resetImage = true;
    }

    // If the camera has changed, reset the sample buffer.
    bool resetCameraXform = false;
    GfMatrix4d viewMatrix = renderPassState->GetWorldToViewMatrix();
    if (_viewMatrix != viewMatrix ) {
        _viewMatrix = viewMatrix;

        resetCameraXform = true;
    }

    bool resetCameraPersp = false;
    GfMatrix4d projMatrix = renderPassState->GetProjectionMatrix();
    if (_projMatrix != projMatrix) {
        _projMatrix = projMatrix;

        resetCameraPersp = true;
    }

    // Create the camera's transform and the all NSI objects.
    if (_cameraXformHandle.empty())
    {
        // Create the camera node and the others.
        _CreateNSICamera();
    }

    // Create a headlight for the scene.
    if (_headlightXformHandle.empty()) {
        _CreateNSIHeadLight();
    }

    // Create the environment light.
    if (_envlightXformHandle.empty()) {
        _CreateNSIEnvironmentLight();
    }

    // Reset the sample buffer if it's been requested.
    if (resetImage) {
        if (_renderStatus == Running)
        {
            StopRender();
        }

        // Update the resolution.
        NSI::ArgumentList args;

        int res_data[2] =
        {
            static_cast<int>(_width),
            static_cast<int>(_height)
        };
        args.Add(NSI::Argument::New("resolution")
            ->SetArrayType(NSITypeInteger, 2)
            ->CopyValue(res_data, sizeof(res_data)));

        // Update the crop.
        float crop_data[2][2] =
        {
            {0, 0},
            {1, 1}
        };
        args.Add(NSI::Argument::New("crop")
            ->SetArrayType(NSITypeFloat, 2)
            ->SetCount(2)
            ->SetValuePointer(crop_data));

        // Update the window.
        double aspect = static_cast<double>(_width) / static_cast<double>(_height);
        double window_data[2][2] =
        {
            {-aspect, -1},
            { aspect,  1}
        };
        args.Add(NSI::Argument::New("screenwindow")
            ->SetArrayType(NSITypeDouble, 2)
            ->SetCount(2)
            ->CopyValue(window_data, sizeof(window_data)));

        nsi->SetAttribute(ScreenHandle(), args);

        if (_renderStatus == Running)
        {
            // Restart the render.
            StartRender();
        }
    }

    // Update the view matrix of camera.
    if (resetCameraXform) {
        // Update the render camera.
        const GfMatrix4d &viewInvMatrix = _viewMatrix.GetInverse();
        nsi->SetAttribute(_cameraXformHandle,
            NSI::DoubleMatrixArg("transformationmatrix",
                viewInvMatrix.GetArray()));

        // Update the headlight.
        const GfVec3d &viewPos = _viewMatrix.ExtractTranslation();
        const GfRotation &viewRotation = _viewMatrix.ExtractRotation();

        GfMatrix4d headlightMatrix(1.0);
        headlightMatrix.SetLookAt(viewPos, viewRotation);

        nsi->SetAttribute(_headlightXformHandle,
            NSI::DoubleMatrixArg("transformationmatrix", headlightMatrix.GetArray()));
    }

    // Update the fov of camera.
    if (resetCameraPersp) {
        _UpdateNSICamera();
    }

    // Launch rendering or synchronize the all changes.
    if (_renderStatus == Stopped)
    {
        StartRender();
        // Change the render status.
        _renderStatus = Running;
    }
    else if (resetImage || resetCameraXform || resetCameraPersp ||
        _renderParam->SceneEdited())
    {
        // Tell 3Delight to update.
        nsi->RenderControl(NSI::CStringPArg("action", "synchronize"));
    }

    /* The renderer is now up to date on all changes. */
    _renderParam->ResetSceneEdited();

    // Blit!
    if (_imageHandle->_buffer.size()) {
        _compositor.UpdateColor(_width, _height, _imageHandle->_buffer.data());
        _compositor.UpdateDepth(_width, _height,
            (uint8_t*)_imageHandle->_depth_buffer.data());
        _compositor.Draw();
    }
}

void HdNSIRenderPass::StopRender() const
{
    _renderParam->GetNSIContext()->RenderControl(
        NSI::CStringPArg("action", "stop"));
}

void HdNSIRenderPass::StartRender() const
{
    _renderParam->GetNSIContext()->RenderControl((
        NSI::CStringPArg("action", "start"),
        NSI::IntegerArg("interactive", 1),
        NSI::IntegerArg("progressive", 1)));
}

std::string HdNSIRenderPass::ScreenHandle() const
{
    return boost::lexical_cast<std::string>(this) + "|screen1";
}

void HdNSIRenderPass::SetOversampling() const
{
    std::shared_ptr<NSI::Context> nsi = _renderParam->AcquireSceneForEdit();

    VtValue s = _renderDelegate->GetRenderSetting(
        HdNSIRenderSettingsTokens->pixelSamples);

    if (_renderStatus == Running)
    {
        StopRender();
    }

    nsi->SetAttribute(ScreenHandle(),
        NSI::IntegerArg("oversampling", s.Get<int>()));

    if (_renderStatus == Running)
    {
        StartRender();
    }
}

void HdNSIRenderPass::_CreateNSICamera()
{
    std::shared_ptr<NSI::Context> nsi = _renderParam->AcquireSceneForEdit();

    // Create the camera node and the others.
    const std::string &prefix = boost::lexical_cast<std::string>(this);

    _cameraXformHandle = prefix + "|camera1";
    nsi->Create(_cameraXformHandle, "transform");
    {
        const GfMatrix4d &viewInvMatrix = _viewMatrix.GetInverse();
        nsi->SetAttribute(_cameraXformHandle,
            NSI::DoubleMatrixArg("transformationmatrix",
                viewInvMatrix.GetArray()));
    }
    nsi->Connect(_cameraXformHandle, "", NSI_SCENE_ROOT, "objects");

    // Create the camera shape.
    // XXX: Support orthographics camera.
    _cameraShapeHandle = prefix + "|cameraShape1";
    nsi->Create(_cameraShapeHandle, "perspectivecamera");
    {
        NSI::ArgumentList args;

        double clipping_range_data[2] =
        {
            0.1,
            10000
        };
        args.Add(NSI::Argument::New("clippingrange")
            ->SetType(NSITypeDouble)
            ->SetCount(2)
            ->SetValuePointer(clipping_range_data));

        nsi->SetAttribute(_cameraShapeHandle, args);
    }
    nsi->Connect(_cameraShapeHandle, "", _cameraXformHandle, "objects");

    _UpdateNSICamera();

    // Create a screen, the output of camera.
    const std::string screenHandle = ScreenHandle();
    nsi->Create(screenHandle, "screen");
    SetOversampling();
    nsi->Connect(screenHandle, "", _cameraShapeHandle, "screens");

    // Create a outputlayer, the format of a color variable.
    std::string layer1Handle = prefix + "|outputLayer1";
    nsi->Create(layer1Handle, "outputlayer");
    {
        nsi->SetAttribute(layer1Handle, (
            NSI::StringArg("variablename", "Ci"),
            NSI::StringArg("layertype", "color"),
            NSI::StringArg("scalarformat", "uint8"),
            NSI::IntegerArg("withalpha", 1),
            NSI::IntegerArg("sortkey", 0),
            NSI::StringArg("variablesource", "shader"),
            NSI::PointerArg("outputhandle", _imageHandle)));
    }
    nsi->Connect(layer1Handle, "", screenHandle, "outputlayers");

    // A second layer for depth.
    std::string layer2Handle = prefix + "|outputLayer2";
    nsi->Create(layer2Handle, "outputlayer");
    {
        nsi->SetAttribute(layer2Handle, (
            NSI::StringArg("variablename", "z"),
            NSI::StringArg("layertype", "scalar"),
            NSI::StringArg("scalarformat", "float"),
            NSI::StringArg("filter", "min"),
            NSI::DoubleArg("filterwidth", 1.0),
            NSI::IntegerArg("sortkey", 1),
            NSI::PointerArg("outputhandle", _imageHandle)));
    }
    nsi->Connect(layer2Handle, "", screenHandle, "outputlayers");

    // Create a displaydriver, the receiver of the computed pixels.
    _outputDriverHandle = prefix + "|outputDriver1";
    nsi->Create(_outputDriverHandle, "outputdriver");
    {
        nsi->SetAttribute(_outputDriverHandle, NSI::StringArg("drivername", "HdNSI"));
        nsi->SetAttribute(_outputDriverHandle, NSI::StringArg("imagefilename", prefix));
    }
    nsi->Connect(_outputDriverHandle, "", layer1Handle, "outputdrivers");
    nsi->Connect(_outputDriverHandle, "", layer2Handle, "outputdrivers");
#ifdef NSI_DEBUG
    std::string debugDriverHandle = prefix + "|debugDriver1";
    {
        nsi->SetAttribute(_outputDriverHandle, NSI::StringArg("drivername", "idisplay"));
        nsi->SetAttribute(_outputDriverHandle, NSI::StringArg("imagefilename", prefix));
    }
    nsi->Connect(debugDriverHandle, "", layer1Handle, "outputdrivers");
#endif
}

std::string HdNSIRenderPass::ExportNSIHeadLightShader()
{
    std::shared_ptr<NSI::Context> nsi = _renderParam->AcquireSceneForEdit();
    const std::string prefix = boost::lexical_cast<std::string>(this);
    std::string handle = + "|headlightShader1";

    nsi->Create(handle, "shader");

    NSI::ArgumentList args;

    const std::string &directionalLightShaderPath =
        _renderDelegate->GetDelight() + "/maya/osl/directionalLight";

    args.Add(new NSI::StringArg("shaderfilename",
        directionalLightShaderPath));

    float light_shader_color_data[3] = { 1, 1, 1 };
    args.Add(new NSI::ColorArg("i_color", light_shader_color_data));

    VtValue intensity = _renderDelegate->GetRenderSetting(
        HdNSIRenderSettingsTokens->cameraLightIntensity);
    /*
        This ugly mess is because we need the initial value to be a float or
        the UI won't build itself. But said UI then sets any new value as a
        double.
    */
    float intensity_value = intensity.IsHolding<float>()
        ? intensity.Get<float>() : intensity.Get<double>();
    args.Add(new NSI::FloatArg("intensity", intensity_value));

    args.Add(new NSI::FloatArg("diffuse_contribution", 1));
    args.Add(new NSI::FloatArg("specular_contribution", 1));

    nsi->SetAttribute(handle, args);
    return handle;
}

void HdNSIRenderPass::_CreateNSIHeadLight()
{
    std::shared_ptr<NSI::Context> nsi = _renderParam->AcquireSceneForEdit();

    // Create the transform node.
    const std::string &prefix = boost::lexical_cast<std::string>(this);
    _headlightXformHandle = prefix + "|headlight1";
    nsi->Create(_headlightXformHandle, "transform");
    {
        const GfMatrix4d &viewInvMatrix = _viewMatrix.GetInverse();

        const GfVec3d &viewPos = viewInvMatrix.ExtractTranslation();
        const GfRotation &viewRotation = viewInvMatrix.ExtractRotation();

        // This transform is calculated from camera transform.
        GfMatrix4d headlightMatrix(1.0);
        headlightMatrix.SetLookAt(viewPos, viewRotation.GetInverse());

        nsi->SetAttribute(_headlightXformHandle,
            NSI::DoubleMatrixArg("transformationmatrix", headlightMatrix.GetArray()));
    }
    nsi->Connect(_headlightXformHandle, "", NSI_SCENE_ROOT, "objects");

    // Create the headlight shape node.
    _headlightShapeHandle = prefix + "|headlightShape1";
    nsi->Create(_headlightShapeHandle, "environment");
    {
        nsi->SetAttribute(_headlightShapeHandle,
            NSI::DoubleArg("angle", 0));
    }
    nsi->Connect(_headlightShapeHandle, "", _headlightXformHandle, "objects");

    // Create the geometryattributes node for light.
    _headlightGeoAttrsHandle = _headlightShapeHandle + "Attr1";

    nsi->Create(_headlightGeoAttrsHandle, "attributes");
    nsi->Connect(_headlightGeoAttrsHandle, "", _headlightXformHandle, "geometryattributes");

    // Attach the light shader to the headlight shape.
    std::string headlightShaderHandle = ExportNSIHeadLightShader();
    nsi->Connect(headlightShaderHandle, "", _headlightGeoAttrsHandle, "surfaceshader");
}

void HdNSIRenderPass::_CreateNSIEnvironmentLight()
{
    std::shared_ptr<NSI::Context> nsi = _renderParam->AcquireSceneForEdit();

    const std::string &prefix = boost::lexical_cast<std::string>(this);

    // Handles to all the nodes we might create in here.
    _envlightXformHandle = prefix + "|envlight1";
    std::string envlightShapeHandle = prefix + "|envlightShape1";
    std::string envlightGeoAttrsHandle = envlightShapeHandle + "|attributes1";
    std::string envlightShaderHandle = prefix + "|envlightShader1";
    std::string envlightFileShaderHandle = prefix + "|envlightFileShader1";
    std::string envlightCoordShaderHandle = prefix + "|envCoordShader1";

    // Delete any existing nodes (for update from a setting change).
    nsi->Delete(_envlightXformHandle);
    nsi->Delete(envlightShapeHandle);
    nsi->Delete(envlightGeoAttrsHandle);
    nsi->Delete(envlightShaderHandle);
    nsi->Delete(envlightFileShaderHandle);
    nsi->Delete(envlightCoordShaderHandle);

    // Create the empty transform.
    nsi->Create(_envlightXformHandle, "transform");
    nsi->Connect(_envlightXformHandle, "", NSI_SCENE_ROOT, "objects");

    // Create the shape node.
    nsi->Create(envlightShapeHandle, "environment");
    nsi->Connect(envlightShapeHandle, "", _envlightXformHandle, "objects");

    // Create the geometryattributes node for light.
    nsi->Create(envlightGeoAttrsHandle, "attributes");
    nsi->Connect(envlightGeoAttrsHandle, "", _envlightXformHandle, "geometryattributes");

    // Construct the shader.
    nsi->Create(envlightShaderHandle, "shader");
    nsi->Connect(envlightShaderHandle, "", envlightGeoAttrsHandle, "surfaceshader");

    // Use user-defined environment image or empty.
    float color[3] = {1, 1, 1};

    std::string envLightPath = _renderDelegate->GetRenderSetting(
        HdNSIRenderSettingsTokens->envLightPath).Get<std::string>();
    int envLightMapping = _renderDelegate->GetRenderSetting(
        HdNSIRenderSettingsTokens->envLightMapping).Get<int>();
    VtValue eLI_value = _renderDelegate->GetRenderSetting(
        HdNSIRenderSettingsTokens->envLightIntensity);
    float envLightIntensity = eLI_value.IsHolding<float>()
        ? eLI_value.Get<float>() : eLI_value.Get<double>();
    bool envAsBackground = _renderDelegate->GetRenderSetting(
        HdNSIRenderSettingsTokens->envAsBackground).Get<bool>();
    bool envUseSky = _renderDelegate->GetRenderSetting(
        HdNSIRenderSettingsTokens->envUseSky).Get<bool>();

    if (envLightPath.size() || envUseSky)
    {
        // Set the environment shader.
        const std::string &shaderPath =
            _renderDelegate->GetDelight() + "/maya/osl/dlEnvironmentShape";

        nsi->SetAttribute(envlightShaderHandle, (
            NSI::StringArg("shaderfilename", shaderPath),
            NSI::IntegerArg("mapping", envLightMapping),
            NSI::ColorArg("i_texture", color),
            NSI::FloatArg("intensity", envLightIntensity),
            NSI::FloatArg("exposure", 0),
            NSI::ColorArg("tint", color)));

        // Use the external file.
        nsi->Create(envlightFileShaderHandle, "shader");

        if (envLightPath.size()) {
            // Set the enviroment image.
            const std::string &shaderPath =
                _renderDelegate->GetDelight() + "/maya/osl/file";

            nsi->SetAttribute(envlightFileShaderHandle, (
                NSI::StringArg("shaderfilename", shaderPath),
                NSI::ColorArg("defaultColor", color),
                NSI::CStringPArg("fileTextureName.meta.colorspace", "linear")));

            nsi->SetAttribute(envlightFileShaderHandle,
                NSI::StringArg("fileTextureName", envLightPath));
        } else {
            // Set the sky.
            const std::string &shaderPath =
                _renderDelegate->GetDelight() + "/maya/osl/dlSky";

            nsi->SetAttribute(envlightFileShaderHandle, (
                NSI::StringArg("shaderfilename", shaderPath),
                NSI::FloatArg("intensity", envLightIntensity),
                NSI::FloatArg("turbidity", 3.0f),
                NSI::FloatArg("elevation", 45.0f),
                NSI::FloatArg("azimuth", 90.0f),
                NSI::IntegerArg("sun_enable", 1),
                NSI::FloatArg("sun_size", 0.5f),
                NSI::ColorArg("sky_tint", color),
                NSI::ColorArg("sun_tint", color),
                NSI::FloatArg("wavelengthR", 615),
                NSI::FloatArg("wavelengthG", 545),
                NSI::FloatArg("wavelengthB", 450)));
        }

        nsi->Connect(envlightFileShaderHandle, "outColor",
                envlightShaderHandle, "i_texture");

        // Create the coordinate mapping node.
        nsi->Create(envlightCoordShaderHandle, "shader");
        {
            const std::string &shaderPath =
                _renderDelegate->GetDelight() + "/maya/osl/uvCoordEnvironment";

            nsi->SetAttribute(envlightCoordShaderHandle,
                NSI::StringArg("shaderfilename", shaderPath));

            nsi->SetAttribute(envlightCoordShaderHandle,
                NSI::IntegerArg("mapping", envLightMapping));
        }
        nsi->Connect(envlightCoordShaderHandle, "o_outUV",
            envlightFileShaderHandle, "uvCoord");

        // Check if diplay the environment as background.
        if (envAsBackground) {
            nsi->SetAttribute(envlightGeoAttrsHandle,
                NSI::IntegerArg("visibility.camera", 1));
        }
    } else {
        // Change this environment light to omi light.
        const std::string &shaderPath =
            _renderDelegate->GetDelight() + "/maya/osl/directionalLight";

        nsi->SetAttribute(envlightShaderHandle, (
            NSI::StringArg("shaderfilename", shaderPath),
            NSI::ColorArg("i_color", color),
            NSI::FloatArg("intensity", envLightIntensity),
            NSI::FloatArg("diffuse_contribution", 1),
            NSI::FloatArg("specular_contribution", 1)));
    }
}

void HdNSIRenderPass::_UpdateNSICamera()
{
    std::shared_ptr<NSI::Context> nsi = _renderParam->AcquireSceneForEdit();

    // Calculate the FOV from OpenGL matrix.
    double yScale = _projMatrix[1][1];
    yScale = 1.0 / yScale;
    double fov = atan(yScale) * 2.0;
    fov = GfRadiansToDegrees(fov);

    nsi->SetAttribute(_cameraShapeHandle,
        NSI::FloatArg("fov", fov));
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
