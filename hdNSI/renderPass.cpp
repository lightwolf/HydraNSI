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

#include "pxr/imaging/hdNSI/config.h"
#include "pxr/imaging/hdNSI/mesh.h"

#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/renderPassState.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/work/loops.h"

#include <boost/lexical_cast.hpp>

#include <nsi.hpp>
#include <random>

PXR_NAMESPACE_OPEN_SCOPE

std::mutex HdNSIRenderPass::_imageLock;
HdNSIRenderPass::DspyImageHandle *HdNSIRenderPass::_imageHandle = NULL;

HdNSIRenderPass::HdNSIRenderPass(HdRenderIndex *index,
                                 HdRprimCollection const &collection,
                                 NSIContext_t ctx,
                                 HdNSIRenderParam *renderParam)
    : HdRenderPass(index, collection)
    , _width(0)
    , _height(0)
    , _ctx(ctx)
    , _renderStatus(Stopped)
    , _viewMatrix(1.0f) // == identity
    , _renderParam(renderParam)
    , _sceneVersion(0)
{
}

HdNSIRenderPass::~HdNSIRenderPass()
{
}

bool
HdNSIRenderPass::IsConverged() const
{
    // NSI stops rendering automatically, but this GL loop still works,
    // so we always return false.
    return false;
}

void
HdNSIRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState,
    TfTokenVector const &renderTags)
{
    NSI::Context nsi(_ctx);

    // Create the camera's transform and the all NSI objects.
    if (_cameraXformHandle.empty())
    {
        // Register the display driver.
        //
        PtDspyDriverFunctionTable table;
        memset(&table, 0, sizeof(table));

        table.Version = k_PtDriverCurrentVersion;
        table.pOpen = &_DspyImageOpen;
        table.pQuery = &_DspyImageQuery;
        table.pWrite = &_DspyImageData;
        table.pClose = &_DspyImageClose;

        DspyRegisterDriverTable("HdNSI", &table);

        // Create the camera node and the others.
        const std::string &prefix = boost::lexical_cast<std::string>(this);

        _cameraXformHandle = prefix + "|camera1";
        nsi.Create(_cameraXformHandle, "transform");
        nsi.Connect(_cameraXformHandle, "", NSI_SCENE_ROOT, "objects");

        // Create the camera shape.
        // XXX: Support orthographics camera.
        _cameraShapeHandle = prefix + "|cameraShape1";
        nsi.Create(_cameraShapeHandle, "perspectivecamera");
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

            nsi.SetAttribute(_cameraShapeHandle, args);
        }
        nsi.Connect(_cameraShapeHandle, "", _cameraXformHandle, "objects");

        // Create a screen, the output of camera.
        _screenHandle = prefix + "|screen1";
        nsi.Create(_screenHandle, "screen");
        {
            const HdNSIConfig &config = HdNSIConfig::GetInstance();

            nsi.SetAttribute(_screenHandle, (NSI::IntegerArg("oversampling", config.pixelSamples),
                NSI::FloatArg("pixelaspectratio", 1)));
        }
        nsi.Connect(_screenHandle, "", _cameraShapeHandle, "screens");

        // Create a outputlayer, the format of a color variable.
        _outputLayerHandle = prefix + "|outputLayer1";

        nsi.Create(_outputLayerHandle, "outputlayer");
        {
            nsi.SetAttribute(_outputLayerHandle, (NSI::StringArg("variablename", "Ci"),
                NSI::StringArg("layertype", "color"),
                NSI::StringArg("scalarformat", "uint8"),
                NSI::IntegerArg("withalpha", 1),
                NSI::StringArg("filter", "gaussian"),
                NSI::DoubleArg("filterwidth", 2.0)));
        }
        nsi.Connect(_outputLayerHandle, "", _screenHandle, "outputlayers");

        // Create a displaydriver, the receiver of the computed pixels.
        _outputDriverHandle = prefix + "|outputDriver1";
        nsi.Create(_outputDriverHandle, "outputdriver");
        {
            nsi.SetAttribute(_outputDriverHandle, NSI::StringArg("drivername", "HdNSI"));
            nsi.SetAttribute(_outputDriverHandle, NSI::StringArg("imagefilename", prefix));
        }
        nsi.Connect(_outputDriverHandle, "", _outputLayerHandle, "outputdrivers");

#ifdef _DEBUG
        std::string debugDriverHandle = prefix + "|debugDriver1";
        {
            nsi.SetAttribute(_outputDriverHandle, NSI::StringArg("drivername", "idisplay"));
            nsi.SetAttribute(_outputDriverHandle, NSI::StringArg("imagefilename", prefix));
        }
        nsi.Connect(debugDriverHandle, "", _outputLayerHandle, "outputdrivers");
#endif
    }

    // XXX: Add collection and renderTags support.
    // XXX: Add clip planes support.

    // Track whether the sample buffer is still valid.
    bool resetImage = false;

    int sceneVersion = _renderParam->GetSceneVersion();
    if (_sceneVersion != sceneVersion) {
        _sceneVersion = sceneVersion;

        resetImage = true;
    }

    // If the camera has changed, reset the sample buffer.
    bool resetCameraXform = false;
    GfMatrix4d viewMatrix = renderPassState->GetWorldToViewMatrix().GetInverse();
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

    // If the viewport has changed, resize and reset the sample buffer.
    GfVec4f vp = renderPassState->GetViewport();
    if (_width != vp[2] || _height != vp[3]) {
        _width = vp[2];
        _height = vp[3];

        resetImage = true;
    }

    // Create a headlight for the scene.
    if (_headlightXformHandle.empty()) {
        const std::string &prefix = boost::lexical_cast<std::string>(this);

        // Create the transform node.
        _headlightXformHandle = prefix + "|headlight1";
        nsi.Create(_headlightXformHandle, "transform");
        {
            const GfVec3d &viewPos = _viewMatrix.ExtractTranslation();
            const GfRotation &viewRotation = _viewMatrix.ExtractRotation();

            // This transform is calculated from camera transform.
            GfMatrix4d headlightMatrix(0.0);
            headlightMatrix.SetLookAt(viewPos, viewRotation.GetInverse());

            nsi.SetAttribute(_headlightXformHandle,
                NSI::DoubleMatrixArg("transformationmatrix", headlightMatrix.GetArray()));
        }
        nsi.Connect(_headlightXformHandle, "", NSI_SCENE_ROOT, "objects");

        // Create the shape node.
        _headlightShapeHandle = prefix + "|headlightShape1";
        nsi.Create(_headlightShapeHandle, "environment");
        nsi.Connect(_headlightShapeHandle, "", _headlightXformHandle, "objects");

        // Create the geometryattributes node for light.
        _headlightGeoAttrsHandle = _headlightShapeHandle + "Attr1";

        nsi.Create(_headlightGeoAttrsHandle, "attributes");
        nsi.Connect(_headlightGeoAttrsHandle, "", _headlightXformHandle, "geometryattributes");

        // Attach the light shader to the headlight shape.
        _headlightShaderHandle = prefix + "|headlightShader1";
        nsi.Create(_headlightShaderHandle, "shader");
        {
            NSI::ArgumentList args;

            const HdNSIConfig &config = HdNSIConfig::GetInstance();
            const std::string &directionalLightShaderPath =
                config.delight + "/maya/osl/directionalLight";

            args.Add(new NSI::StringArg("shaderfilename",
                directionalLightShaderPath));

            float light_shader_color_data[3] = { 1, 1, 1 };
            args.Add(new NSI::ColorArg("i_color", light_shader_color_data));

            args.Add(new NSI::FloatArg("intensity",
                config.cameraLightIntensity));

            args.Add(new NSI::FloatArg("diffuse_contribution", 1));
            args.Add(new NSI::FloatArg("specular_contribution", 1));

            nsi.SetAttribute(_headlightShaderHandle, args);
        }
        nsi.Connect(_headlightShaderHandle, "", _headlightGeoAttrsHandle, "surfaceshader");
    }

    // Reset the sample buffer if it's been requested.
    if (resetImage) {
        // Stop the current render.
        nsi.RenderControl(NSI::CStringPArg("action", "stop"));

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
        double aspect = static_cast<double>(_height) / static_cast<double>(_width);
        double window_data[2][2] =
        {
            {-1, -aspect},
            { 1,  aspect}
        };
        args.Add(NSI::Argument::New("screenwindow")
            ->SetArrayType(NSITypeDouble, 2)
            ->SetCount(2)
            ->CopyValue(window_data, sizeof(window_data)));

        nsi.SetAttribute(_screenHandle, args);

        // Restart the render.
        nsi.RenderControl((NSI::CStringPArg("action", "start"),
            NSI::IntegerArg("interactive", 1),
            NSI::IntegerArg("progressive", 1)));
    }

    // Update the view matrix of camera.
    if (resetCameraXform) {
        nsi.SetAttribute(_cameraXformHandle,
            NSI::DoubleMatrixArg("transformationmatrix",
                _viewMatrix.GetArray()));
    }

    // Update the fov of camera.
    if (resetCameraPersp) {
        double yScale = _projMatrix[1][1];
        yScale = 1.0 / yScale;
        double fov = atan(yScale) * 2.0;
        fov = GfRadiansToDegrees(fov);

        nsi.SetAttribute(_cameraShapeHandle,
            NSI::FloatArg("fov", fov));
    }

    // Launch rendering or synchronize the all changes.
    if (_renderStatus == Stopped)
    {
        NSI::ArgumentList args;
        args.Add(new NSI::CStringPArg("action", "start"));
        args.Add(new NSI::IntegerArg("interactive", 1));
        args.Add(new NSI::IntegerArg("progressive", 1));

        nsi.RenderControl(args);

        // Change the render status.
        _renderStatus = Running;
    }
    else if (resetImage || resetCameraXform || resetCameraPersp)
    {
        // Tell 3Delight to update.
        nsi.RenderControl(NSI::CStringPArg("action", "synchronize"));
    }

    // Blit!
    if (_imageHandle) {
        glDrawPixels(_width, _height, GL_RGBA, GL_UNSIGNED_BYTE, _imageHandle->_buffer.data());
    }
}

PtDspyError HdNSIRenderPass::_DspyImageOpen(PtDspyImageHandle *phImage,
                                            const char *driverName,
                                            const char *fileName,
                                            int width, int height,
                                            int paramCount,
                                            const UserParameter *parameters,
                                            int numFormats,
                                            PtDspyDevFormat formats[],
                                            PtFlagStuff *flagStuff)
{
    if(!phImage) {
        return PkDspyErrorBadParams;
    }

    for(int i = 0; i < numFormats; ++ i) {
        formats[i].type = PkDspyUnsigned8;
    }

    std::lock_guard<std::mutex> lock(_imageLock);
    _imageHandle = new DspyImageHandle;

    _imageHandle->_width = width;
    _imageHandle->_height = height;

    for(int i = 0;i < paramCount; ++ i)
    {
        const UserParameter *parameter = parameters + i;

        const std::string &param_name = parameter->name;
        if (param_name == "OriginalSize")
        {
            const int *originalSize = static_cast<const int *>(parameter->value);
            _imageHandle->_originalSizeX = originalSize[0];
            _imageHandle->_originalSizeY = originalSize[1];
        }
        else if (param_name == "origin")
        {
            const int *origin = static_cast<const int *>(parameter->value);
            _imageHandle->_originX = origin[0];
            _imageHandle->_originY = origin[1];
        }
    }

    _imageHandle->_numFormats = numFormats;

    _imageHandle->_buffer.resize(width * height * numFormats, 0);

    *phImage = _imageHandle;

    return PkDspyErrorNone;
}

PtDspyError HdNSIRenderPass::_DspyImageQuery(PtDspyImageHandle hImage,
                                             PtDspyQueryType type,
                                             int dataLen,
                                             void *data)
{
    if(!data && type != PkStopQuery)
    {
        return PkDspyErrorBadParams;
    }

    switch(type)
    {
        case PkSizeQuery:
        {
            PtDspySizeInfo size_info;
            size_info.width = 256;
            size_info.height = 256;
            size_info.aspectRatio = 1;
            memcpy(data, &size_info, sizeof(size_info));

            break;
        }
        case PkOverwriteQuery:
        {
            PtDspyOverwriteInfo info;
            info.overwrite = 1;
            memcpy(data, &info, dataLen > (int)sizeof(info) ? sizeof(info) : (size_t)dataLen);

            break;
        }
        case PkProgressiveQuery:
        {
            if(dataLen < (int)sizeof(PtDspyProgressiveInfo))
            {
                return PkDspyErrorBadParams;
            }
            reinterpret_cast<PtDspyProgressiveInfo *>(data)->acceptProgressive = 1;

            break;
        }
        case PkCookedQuery:
        {
            PtDspyCookedInfo info;
            info.cooked = 1;

            memcpy(data, &info, dataLen > (int)sizeof(info) ? sizeof(info) : (size_t)dataLen);

            break;
        }
        case PkStopQuery:
        {
            return PkDspyErrorNone;

            break;
        }
        case PkThreadQuery:
        {
            PtDspyThreadInfo info;
            info.multithread = 1;

            assert(dataLen >= sizeof(info));
            memcpy(data, &info, sizeof(info));

            break;
        }

        default:
        {
            return PkDspyErrorUnsupported;
        }
    }

    return PkDspyErrorNone;
}

PtDspyError HdNSIRenderPass::_DspyImageData(PtDspyImageHandle hImage,
                                            int xMin, int xMaxPlusOne,
                                            int yMin, int yMaxPlusOne,
                                            int entrySize,
                                            const unsigned char *cdata)
{
    if (!entrySize || !cdata) {
        return PkDspyErrorStop;
    }

    if (!_imageHandle) {
        return PkDspyErrorStop;
    }

    int i = 0;

    for (int y = yMin; y < yMaxPlusOne; ++ y) {
        for (int x = xMin; x < xMaxPlusOne; ++ x) {
            size_t p = x + (_imageHandle->_height - y - 1) * _imageHandle->_width;
            size_t dstOffset = p * _imageHandle->_numFormats;

            _imageHandle->_buffer[dstOffset + 0] = cdata[i * entrySize + 0];
            _imageHandle->_buffer[dstOffset + 1] = cdata[i * entrySize + 1];
            _imageHandle->_buffer[dstOffset + 2] = cdata[i * entrySize + 2];
            _imageHandle->_buffer[dstOffset + 3] = cdata[i * entrySize + 3];

            ++ i;
        }
    }

    return PkDspyErrorNone;
}

PtDspyError HdNSIRenderPass::_DspyImageClose(PtDspyImageHandle hImage)
{
    std::lock_guard<std::mutex> lock(_imageLock);
    delete _imageHandle, _imageHandle = NULL;

    return PkDspyErrorNone;
}

PXR_NAMESPACE_CLOSE_SCOPE
