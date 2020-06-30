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

#include "renderDelegate.h"

#include "camera.h"
#include "curves.h"
#include "field.h"
#include "instancer.h"
#include "light.h"
#include "material.h"
#include "mesh.h"
#include "pointcloud.h"
#include "renderBuffer.h"
#include "renderParam.h"
#include "renderPass.h"
#include "tokens.h"
#include "volume.h"

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/thisPlugin.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/extComputation.h>
#include <pxr/imaging/hd/resourceRegistry.h>

#include <delight.h>

#include <iostream>
#include <cassert>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
	/* This is in usdVolImaging but we shouldn't be using it directly. */
    (openvdbAsset)
);

const TfTokenVector HdNSIRenderDelegate::SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->mesh,
    HdPrimTypeTokens->points,
    HdPrimTypeTokens->basisCurves,
    HdPrimTypeTokens->volume,
};

const TfTokenVector HdNSIRenderDelegate::SUPPORTED_SPRIM_TYPES =
{
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->material,
    HdPrimTypeTokens->cylinderLight,
    HdPrimTypeTokens->diskLight,
    HdPrimTypeTokens->distantLight,
    HdPrimTypeTokens->domeLight,
    HdPrimTypeTokens->rectLight,
    HdPrimTypeTokens->sphereLight,
    HdPrimTypeTokens->extComputation,
};

const TfTokenVector HdNSIRenderDelegate::SUPPORTED_BPRIM_TYPES =
{
    HdPrimTypeTokens->renderBuffer,
    _tokens->openvdbAsset,
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

HdNSIRenderDelegate::HdNSIRenderDelegate(
    HdRenderSettingsMap const& settingsMap)
:
    HdRenderDelegate{settingsMap}
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

    /* Figure out where our shaders are. */
    PlugPluginPtr plugin = PLUG_THIS_PLUGIN;
    _shaders_path = PlugFindPluginResource(plugin, "osl", false);

    decltype(&DlGetLibNameAndVersionString) PDlGetLibNameAndVersionString;
    _capi->LoadFunction(PDlGetLibNameAndVersionString,
        "DlGetLibNameAndVersionString");
    if (PDlGetLibNameAndVersionString) {
        TF_STATUS("hdNSI is using %s at '%s'",
            PDlGetLibNameAndVersionString(), _delight.c_str());
    }

    _capi->LoadFunction(m_DlGetShaderInfo, "DlGetShaderInfo");

    _nsi = std::make_shared<NSI::Context>(*_capi);
    NSI::ArgumentList beginArgs;
    std::string trace_file = TfGetenv("HDNSI_TRACE");
    if (!trace_file.empty())
    {
        beginArgs.push(new NSI::StringArg("streamfilename", trace_file));
    }
    _nsi->Begin(beginArgs);

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
        "Volume Samples",
        HdNSIRenderSettingsTokens->volumeSamples,
        VtValue(TfGetenvInt("HDNSI_VOLUME_SAMPLES", 32))});

    _settingDescriptors.push_back({
        "Pixel Samples",
        HdNSIRenderSettingsTokens->pixelSamples,
        VtValue(TfGetenvInt("HDNSI_PIXEL_SAMPLES", 8))});

    _settingDescriptors.push_back({
        "Maximum Diffuse Depth",
        HdNSIRenderSettingsTokens->maximumDiffuseDepth, VtValue(2)});

    _settingDescriptors.push_back({
        "Maximum Reflection Depth",
        HdNSIRenderSettingsTokens->maximumReflectionDepth, VtValue(2)});

    _settingDescriptors.push_back({
        "Maximum Refraction Depth",
        HdNSIRenderSettingsTokens->maximumRefractionDepth, VtValue(4)});

    _settingDescriptors.push_back({
        "Maximum Hair Depth",
        HdNSIRenderSettingsTokens->maximumHairDepth, VtValue(5)});

    _settingDescriptors.push_back({
        "Maximum Distance",
        HdNSIRenderSettingsTokens->maximumDistance, VtValue(1000.0f)} );

    _settingDescriptors.push_back({
        "Camera light intensity",
        HdNSIRenderSettingsTokens->cameraLightIntensity,
        VtValue(float(TfGetenvDouble("HDNSI_CAMERA_LIGHT_INTENSITY", 1.0)))});

    _PopulateDefaultSettings(_settingDescriptors);

    _exportedSettings = _settingsMap;

    // Set global parameters.
    SetShadingSamples();
    SetVolumeSamples();

    SetMaxDiffuseDepth();
    SetMaxReflectionDepth();
    SetMaxRefractionDepth();
    SetMaxHairDepth();
    SetMaxDistance();


    _nsi->SetAttribute(NSI_SCENE_GLOBAL,(
        NSI::StringArg("bucketorder", "spiral"),
        NSI::IntegerArg("renderatlowpriority", 1)));

    ExportDefaultMaterial();
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

TfToken HdNSIRenderDelegate::GetMaterialBindingPurpose() const
{
    /* Need this to get Material delegates instead of HydraPbsSurface. */
    return HdTokens->full;
}

TfToken HdNSIRenderDelegate::GetMaterialNetworkSelector() const
{
    static const TfToken nsi{"nsi"};
    return nsi;
}

TfTokenVector HdNSIRenderDelegate::GetShaderSourceTypes() const
{
    return HdRenderDelegate::GetShaderSourceTypes();
}

void HdNSIRenderDelegate::SetRenderSetting(
    TfToken const& key,
    VtValue const& value)
{
    VtValue newvalue = value;
    /* Houdini stubbornly sends long for its viewport settings. Convert. */
    if( newvalue.IsHolding<long>() )
    {
        newvalue.Cast<int>();
    }

    HdRenderDelegate::SetRenderSetting(key, newvalue);

    /* See if something actually changed. */
    if( _exportedSettings[key] == newvalue )
        return;

    /* Handle the change. Some are done here, most in the render pass. */
    if (key == HdNSIRenderSettingsTokens->shadingSamples)
    {
        SetShadingSamples();
    }
    if (key == HdNSIRenderSettingsTokens->volumeSamples)
    {
        SetVolumeSamples();
    }
    if( key == HdNSIRenderSettingsTokens->maximumDiffuseDepth )
    {
        SetMaxDiffuseDepth();
    }
    if( key == HdNSIRenderSettingsTokens->maximumReflectionDepth )
    {
        SetMaxReflectionDepth();
    }
    if( key == HdNSIRenderSettingsTokens->maximumRefractionDepth )
    {
        SetMaxRefractionDepth();
    }
    if( key == HdNSIRenderSettingsTokens->maximumHairDepth )
    {
        SetMaxHairDepth();
    }
    if( key == HdNSIRenderSettingsTokens->maximumDistance )
    {
        SetMaxDistance();
    }
    for (HdNSIRenderPass *pass : _renderPasses)
    {
        pass->RenderSettingChanged(key);
    }

    _exportedSettings[key] = newvalue;
}

HdRenderSettingDescriptorList
HdNSIRenderDelegate::GetRenderSettingDescriptors() const
{
    return _settingDescriptors;
}

HdAovDescriptor HdNSIRenderDelegate::GetDefaultAovDescriptor(
    TfToken const& name) const
{
    if (name == HdAovTokens->color)
    {
        return HdAovDescriptor(HdFormatUNorm8Vec4, true, VtValue());
    }
    else if (name == HdAovTokens->depth)
    {
        return HdAovDescriptor(HdFormatFloat32, true, VtValue(1.0f));
    }
#if defined(PXR_VERSION) && PXR_VERSION <= 1911
    else if (name == HdAovTokens->linearDepth)
#else
    else if (name == HdAovTokens->cameraDepth)
#endif
    {
        return HdAovDescriptor(HdFormatFloat32, true, VtValue(0.0f));
    }
    else if (name == HdAovTokens->normal ||
             name == HdAovTokens->Neye)
    {
        return HdAovDescriptor(HdFormatFloat32Vec3, true, VtValue());
    }
    else if (name == HdAovTokens->primId ||
             name == HdAovTokens->instanceId ||
             name == HdAovTokens->elementId)
    {
        return HdAovDescriptor(HdFormatInt32, true, VtValue(-1));
    }
    else
    {
        HdParsedAovToken aovId(name);
        if (aovId.isPrimvar)
        {
            return HdAovDescriptor(HdFormatFloat32Vec3, true, VtValue());
        }
    }

    return HdAovDescriptor();
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
    } else if (typeId == HdPrimTypeTokens->volume) {
        return new HdNSIVolume(rprimId, instancerId);
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
    if (typeId == HdPrimTypeTokens->camera)
    {
        return new HdNSICamera(sprimId);
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
    else if (typeId == HdPrimTypeTokens->material)
    {
        return new HdNSIMaterial(sprimId);
    }
    else if (typeId == HdPrimTypeTokens->extComputation)
    {
        return new HdExtComputation(sprimId);
    }
    else
    {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}

HdSprim *
HdNSIRenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    // For fallback sprims, create objects with an empty scene path.
    // They'll use default values and won't be updated by a scene delegate.
    if (typeId == HdPrimTypeTokens->camera)
    {
        return new HdNSICamera(SdfPath::EmptyPath());
    }
    else if (typeId == HdPrimTypeTokens->material)
    {
        /* I don't think we have any use for this. */
        return nullptr;
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
    else if (typeId == HdPrimTypeTokens->extComputation)
    {
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
    if (typeId == HdPrimTypeTokens->renderBuffer)
    {
        return new HdNSIRenderBuffer(bprimId);
    }
    if (typeId == _tokens->openvdbAsset)
    {
        return new HdNSIField(bprimId);
    }
    TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    return nullptr;
}

HdBprim *
HdNSIRenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer)
    {
        return new HdNSIRenderBuffer(SdfPath::EmptyPath());
    }
    if (typeId == _tokens->openvdbAsset)
    {
        return nullptr;
    }
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

const std::string HdNSIRenderDelegate::FindShader(const std::string &id) const
{
    std::string filename = id + ".oso";

    /* First, try our own shaders. */
    std::string path = TfStringCatPaths(_shaders_path, filename);
    if (TfIsFile(path, true))
        return path;

    /* Try the ones shipped with the renderer. */
    path = TfStringCatPaths(TfStringCatPaths(_delight, "osl"), filename);
    if (TfIsFile(path, true))
        return path;

    /* Nothing found. Return the id. Could be useful for debugging. */
    return id;
}

/*
    Given a shader path, this returns a metadata object for the shader.
*/
DlShaderInfo* HdNSIRenderDelegate::GetShaderInfo(
    const std::string &i_shader) const
{
    if (!m_DlGetShaderInfo)
        return nullptr;

    return m_DlGetShaderInfo(i_shader.c_str());
}

/*
    Returns true if this is a batch UsdRender job.

    It's not clear if there's an official way to check this. For now, use a
    setting provided by Houdini's husk.
*/
bool HdNSIRenderDelegate::IsBatch() const
{
    static const TfToken renderMode{"renderMode"};
    static const TfToken batch{"batch"};
    return GetRenderSetting(renderMode) == batch;
}

void HdNSIRenderDelegate::SetShadingSamples() const
{
    VtValue s = GetRenderSetting(HdNSIRenderSettingsTokens->shadingSamples);

    _nsi->SetAttribute(NSI_SCENE_GLOBAL,
        NSI::IntegerArg("quality.shadingsamples", s.Get<int>()));
}

void HdNSIRenderDelegate::SetVolumeSamples() const
{
    VtValue s = GetRenderSetting(HdNSIRenderSettingsTokens->volumeSamples);

    _nsi->SetAttribute(NSI_SCENE_GLOBAL,
        NSI::IntegerArg("quality.volumesamples", s.Get<int>()));
}

void HdNSIRenderDelegate::SetMaxDiffuseDepth() const
{
    VtValue s = GetRenderSetting(HdNSIRenderSettingsTokens->maximumDiffuseDepth);

    _nsi->SetAttribute(NSI_SCENE_GLOBAL,
        NSI::IntegerArg("maximumraydepth.diffuse", s.Get<int>()));
}

void HdNSIRenderDelegate::SetMaxReflectionDepth() const
{
    VtValue s = GetRenderSetting(HdNSIRenderSettingsTokens->maximumReflectionDepth);

    _nsi->SetAttribute(NSI_SCENE_GLOBAL,
        NSI::IntegerArg("maximumraydepth.reflection", s.Get<int>()));
}

void HdNSIRenderDelegate::SetMaxRefractionDepth() const
{
    VtValue s = GetRenderSetting(HdNSIRenderSettingsTokens->maximumRefractionDepth);

    _nsi->SetAttribute(NSI_SCENE_GLOBAL,
        NSI::IntegerArg("maximumraydepth.refraction", s.Get<int>()));
}

void HdNSIRenderDelegate::SetMaxHairDepth() const
{
    VtValue s = GetRenderSetting(HdNSIRenderSettingsTokens->maximumHairDepth);

    _nsi->SetAttribute(NSI_SCENE_GLOBAL,
        NSI::IntegerArg("maximumraydepth.hair", s.Get<int>()));
}

void HdNSIRenderDelegate::SetMaxDistance() const
{
    VtValue s = GetRenderSetting(HdNSIRenderSettingsTokens->maximumDistance);
    double l = s.IsHolding<float>() ? s.Get<float>() : s.Get<double>();

    _nsi->SetAttribute(NSI_SCENE_GLOBAL,
        NSI::DoubleArg("maximumraylength.diffuse", l));
}

/*
    Export a simple shading network which is used as the default material when
    none is assigned to a primitive.
*/
void HdNSIRenderDelegate::ExportDefaultMaterial() const
{
    std::string baseHandle = DefaultMaterialHandle();
    std::string shaderHandle = baseHandle + "|PreviewSurface";
    std::string colorHandle = baseHandle + "|ColorReader";
    std::string opacityHandle = baseHandle + "|OpacityReader";
    _nsi->Create(baseHandle, "attributes");

    _nsi->Create(shaderHandle, "shader");
    _nsi->SetAttribute(shaderHandle,
        NSI::StringArg("shaderfilename", FindShader("UsdPreviewSurface")));
    _nsi->Connect(shaderHandle, "", baseHandle, "surfaceshader");

    /* Read 'displayColor' primvar and use as diffuse color. */
    _nsi->Create(colorHandle, "shader");
    float fallback[3] = {1.0f, 1.0f, 1.0f};
    _nsi->SetAttribute(colorHandle, (
        NSI::StringArg("shaderfilename",
            FindShader("UsdPrimvarReader_float3")),
        NSI::StringArg("varname", "displayColor"),
        NSI::ColorArg("fallback", fallback)
        ));
    _nsi->Connect(colorHandle, "result", shaderHandle, "diffuseColor");

    /* Read 'displayOpacity' primvar and use as opacity. */
    _nsi->Create(opacityHandle, "shader");
    _nsi->SetAttribute(opacityHandle, (
        NSI::StringArg("shaderfilename",
            FindShader("UsdPrimvarReader_float")),
        NSI::StringArg("varname", "displayOpacity"),
        NSI::FloatArg("fallback", 1.0f)
        ));
    _nsi->Connect(opacityHandle, "result", shaderHandle, "opacity");
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
