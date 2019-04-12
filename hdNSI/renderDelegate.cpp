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
#include "pxr/base/tf/getenv.h"
#include "pxr/imaging/glf/glew.h"
#include "pxr/imaging/hdNSI/renderDelegate.h"

#include "pxr/imaging/hdNSI/instancer.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/imaging/hdNSI/renderPass.h"
#include "pxr/imaging/hdNSI/tokens.h"

#include "pxr/imaging/hd/resourceRegistry.h"

#include "pxr/imaging/hdNSI/mesh.h"
#include "pxr/imaging/hdNSI/pointcloud.h"
#include "pxr/imaging/hdNSI/curves.h"
//XXX: Add other Rprim types later
#include "pxr/imaging/hd/camera.h"
//XXX: Add other Sprim types later
#include "pxr/imaging/hd/bprim.h"
#include "pxr/imaging/hdNSI/light.h"
//XXX: Add bprim types

#include "delight.h"

#include <iostream>
#include <cassert>

PXR_NAMESPACE_OPEN_SCOPE

const TfTokenVector HdNSIRenderDelegate::SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->mesh,
    HdPrimTypeTokens->points,
    HdPrimTypeTokens->basisCurves,
};

const TfTokenVector HdNSIRenderDelegate::SUPPORTED_SPRIM_TYPES =
{
    HdPrimTypeTokens->camera,
    //HdPrimTypeTokens->cylinderLight,
    //HdPrimTypeTokens->diskLight,
    HdPrimTypeTokens->distantLight,
    //HdPrimTypeTokens->domeLight,
    //HdPrimTypeTokens->rectLight,
    //HdPrimTypeTokens->sphereLight
};

const TfTokenVector HdNSIRenderDelegate::SUPPORTED_BPRIM_TYPES =
{
};

std::mutex HdNSIRenderDelegate::_mutexResourceRegistry;
std::atomic_int HdNSIRenderDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr HdNSIRenderDelegate::_resourceRegistry;

/*static void nsi_error_handler(void *userdata, int level, int code, const char *message)
{
    HdNSIRenderDelegate *rDel = static_cast<HdNSIRenderDelegate *>(userdata);
    rDel->HandleNSIError(level, code, message);
}*/

/* static */
void
HdNSIRenderDelegate::HandleNSIError(int level, int code, const char* msg)
{
    // Forward NSI error messages through to hydra logging.
    switch (level) {
        case NSIErrMessage:
            std::cerr << "NSI message code " << code << ": " << msg << std::endl;
            break;
        case NSIErrInfo:
            std::cerr << "NSI info code " << code << ": " << msg << std::endl;
            break;
        case NSIErrWarning:
            std::cerr << "NSI warning code " << code << ": " << msg << std::endl;
            break;
        case NSIErrError:
            TF_CODING_ERROR("NSI error code %d: %s", code, msg);
            break;
        default:
            TF_CODING_ERROR("NSI invalid error level:%d code:%d - %s", level, code, msg);
            break;
    }
}

HdNSIRenderDelegate::HdNSIRenderDelegate()
{
    // Initialize the NSI context with dynamic API.
    _capi.reset(new NSI::DynamicAPI);

    /* Init output driver too. */
    HdNSIOutputDriver::Register(*_capi);

    /* Init install root path. */
    decltype(&DlGetInstallRoot) PDlGetInstallRoot;
    _capi->LoadFunction(PDlGetInstallRoot, "DlGetInstallRoot");
    if (PDlGetInstallRoot) {
        _delight = PDlGetInstallRoot();
    }

    decltype(&DlGetLibNameAndVersionString) PDlGetLibNameAndVersionString;
    _capi->LoadFunction(PDlGetLibNameAndVersionString,
        "DlGetLibNameAndVersionString");
    if (PDlGetLibNameAndVersionString) {
        TF_STATUS("hdNSI is using %s at '%s'",
            PDlGetLibNameAndVersionString(), _delight.c_str());
    }

    _nsi = std::make_shared<NSI::Context>(*_capi);
    _nsi->Begin();

    // Store top-level NSI objects inside a render param that can be
    // passed to prims during Sync().
    _renderParam = std::make_shared<HdNSIRenderParam>(this, _nsi);

    // Initialize one resource registry for all NSI plugins
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);

    if (_counterResourceRegistry.fetch_add(1) == 0) {
        _resourceRegistry.reset( new HdResourceRegistry() );
    }

    // Fill in settings.
    _settingDescriptors.push_back({
        "Shading Samples",
        HdNSIRenderSettingsTokens->shadingSamples,
        VtValue(TfGetenvInt("HDNSI_SHADING_SAMPLES", 64))});

    _settingDescriptors.push_back({
        "Pixel Samples",
        HdNSIRenderSettingsTokens->pixelSamples,
        VtValue(TfGetenvInt("HDNSI_PIXEL_SAMPLES", 8))});

    _settingDescriptors.push_back({
        "Camera light intensity",
        HdNSIRenderSettingsTokens->cameraLightIntensity,
        VtValue(float(TfGetenvDouble("HDNSI_CAMERA_LIGHT_INTENSITY", 1.0)))});

    _settingDescriptors.push_back({
        "Environment image",
        HdNSIRenderSettingsTokens->envLightPath,
        VtValue(TfGetenv("HDNSI_ENV_LIGHT_IMAGE"))});

    _settingDescriptors.push_back({
        "Format of enviroment image, spherical (0) or angular (1)",
        HdNSIRenderSettingsTokens->envLightMapping,
        VtValue(TfGetenvInt("HDNSI_ENV_LIGHT_MAPPING", 0))});

    _settingDescriptors.push_back({
        "Intensity of enviroment image",
        HdNSIRenderSettingsTokens->envLightIntensity,
        VtValue(float(TfGetenvDouble("HDNSI_ENV_LIGHT_INTENSITY", 1.0)))});

    _settingDescriptors.push_back({
        "Display environment image as background",
        HdNSIRenderSettingsTokens->envAsBackground,
        VtValue(TfGetenvBool("HDNSI_ENV_AS_BACKGROUND", true))});

    _settingDescriptors.push_back({
        "Use 3Delight Sky as environment",
        HdNSIRenderSettingsTokens->envUseSky,
        VtValue(TfGetenvBool("HDNSI_ENV_USE_SKY", true))});

    _PopulateDefaultSettings(_settingDescriptors);

    _exportedSettings = _settingsMap;

    // Set global parameters.
    SetShadingSamples();

    _nsi->SetAttribute(NSI_SCENE_GLOBAL,
        NSI::StringArg("bucketorder", "spiral"));
}

HdNSIRenderDelegate::~HdNSIRenderDelegate()
{
    // Clean the resource registry only when it is the last NSI delegate
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);

    if (_counterResourceRegistry.fetch_sub(1) == 1) {
        _resourceRegistry.reset();
    }

    // Destroy NSI context.
    _renderParam.reset();
}

HdRenderParam*
HdNSIRenderDelegate::GetRenderParam() const
{
    return _renderParam.get();
}

void
HdNSIRenderDelegate::CommitResources(HdChangeTracker *tracker)
{
    // CommitResources() is called after prim sync has finished, but before any
    // tasks (such as draw tasks) have run. HdNSI primitives have already
    // updated NSI buffer pointers and dirty state in prim Sync(). ~~but we
    // still need to rebuild acceleration datastructures here with nsiCommit().~~
    //
    // During task execution, the NSI scene is treated as read-only by the
    // drawing code; the BVH won't be updated until the next time through
    // HdEngine::Execute().
    // XXX TODO: does NSI need a sort of commit? don't think so...
    // nsiCommit(_nsi_ctx);
}

void HdNSIRenderDelegate::SetRenderSetting(
    TfToken const& key,
    VtValue const& value)
{
    HdRenderDelegate::SetRenderSetting(key, value);

    /* See if something actually changed. */
    if( _exportedSettings[key] == value )
        return;

    /* Handle the change. Some are done here, most in the render pass. */
    if (key == HdNSIRenderSettingsTokens->shadingSamples)
    {
        SetShadingSamples();
    }
    for (HdNSIRenderPass *pass : _renderPasses)
    {
        pass->RenderSettingChanged(key);
    }

    _exportedSettings[key] = value;
    _nsi->RenderControl(NSI::CStringPArg("action", "synchronize"));
}

HdRenderSettingDescriptorList
HdNSIRenderDelegate::GetRenderSettingDescriptors() const
{
    return _settingDescriptors;
}

TfTokenVector const&
HdNSIRenderDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const&
HdNSIRenderDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const&
HdNSIRenderDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr
HdNSIRenderDelegate::GetResourceRegistry() const
{
    return _resourceRegistry;
}

HdRenderPassSharedPtr
HdNSIRenderDelegate::CreateRenderPass(HdRenderIndex *index,
                                      HdRprimCollection const& collection)
{
    auto pass = new HdNSIRenderPass(
        index, collection, this, _renderParam.get());
    _renderPasses.push_back(pass);
    return HdRenderPassSharedPtr(pass);
}

HdInstancer *
HdNSIRenderDelegate::CreateInstancer(HdSceneDelegate *delegate,
                                        SdfPath const& id,
                                        SdfPath const& instancerId)
{
    return new HdNSIInstancer(delegate, id, instancerId);
}

void
HdNSIRenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
    delete instancer;
}

HdRprim *
HdNSIRenderDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId,
                                    SdfPath const& instancerId)
{
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdNSIMesh(rprimId, instancerId);
    } else if (typeId == HdPrimTypeTokens->points) {
        return new HdNSIPointCloud(rprimId, instancerId);
    } else if (typeId == HdPrimTypeTokens->basisCurves) {
        return new HdNSICurves(rprimId, instancerId);
    } else {
        TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
    }

    return nullptr;
}

void
HdNSIRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    delete rPrim;
}

HdSprim *
HdNSIRenderDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(sprimId);
    }
    else if (
        typeId == HdPrimTypeTokens->cylinderLight ||
        typeId == HdPrimTypeTokens->diskLight ||
        typeId == HdPrimTypeTokens->distantLight ||
        typeId == HdPrimTypeTokens->domeLight ||
        typeId == HdPrimTypeTokens->rectLight ||
        typeId == HdPrimTypeTokens->sphereLight )
    {
        return new HdNSILight(typeId, sprimId);
    }
    else if (typeId == HdPrimTypeTokens->material) {
    } else {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}

HdSprim *
HdNSIRenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    // For fallback sprims, create objects with an empty scene path.
    // They'll use default values and won't be updated by a scene delegate.
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(SdfPath::EmptyPath());
    }
    else if (
        typeId == HdPrimTypeTokens->cylinderLight ||
        typeId == HdPrimTypeTokens->diskLight ||
        typeId == HdPrimTypeTokens->distantLight ||
        typeId == HdPrimTypeTokens->domeLight ||
        typeId == HdPrimTypeTokens->rectLight ||
        typeId == HdPrimTypeTokens->sphereLight )
    {
        /* Not sure this is of any use to us so don't create any for now. */
        return nullptr;
    }
    else
    {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}

void
HdNSIRenderDelegate::DestroySprim(HdSprim *sPrim)
{
    delete sPrim;
}

HdBprim *
HdNSIRenderDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
    TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    return nullptr;
}

HdBprim *
HdNSIRenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    return nullptr;
}

void
HdNSIRenderDelegate::DestroyBprim(HdBprim *bPrim)
{
    delete bPrim;
}

void HdNSIRenderDelegate::RemoveRenderPass(HdNSIRenderPass *renderPass)
{
    _renderPasses.erase(
        std::remove(_renderPasses.begin(), _renderPasses.end(), renderPass),
        _renderPasses.end());
}

void HdNSIRenderDelegate::SetShadingSamples() const
{
    VtValue s = GetRenderSetting(HdNSIRenderSettingsTokens->shadingSamples);

    _nsi->SetAttribute(NSI_SCENE_GLOBAL,
        NSI::IntegerArg("quality.shadingsamples", s.Get<int>()));
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
