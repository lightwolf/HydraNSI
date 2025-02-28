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
#ifndef HDNSI_RENDER_DELEGATE_H
#define HDNSI_RENDER_DELEGATE_H

#include "compatibility.h"

#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/pxr.h>

#include <3Delight/Progress.h>
#include <3Delight/ShaderQuery.h>
#include <nsi_dynamic.hpp>

#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderParam;
class HdNSIRenderPass;

///
/// \class HdNSIRenderDelegate
///
/// Render delegates provide renderer-specific functionality to the render
/// index, the main hydra state management structure. The render index uses
/// the render delegate to create and delete scene primitives, which include
/// geometry and also non-drawable objects. The render delegate is also
/// responsible for creating renderpasses, which know how to draw this
/// renderer's scene primitives.
///
/// Primitives in Hydra are split into Rprims (drawables), Sprims (state
/// objects like cameras and materials), and Bprims (buffer objects like
/// textures). The minimum set of primitives a renderer needs to support is
/// one Rprim (so the scene's not empty) and the "camera" Sprim, which is
/// required by HdxRenderTask, the task implementing basic hydra drawing.
///
/// A render delegate can report which prim types it supports via
/// GetSupportedRprimTypes() (and Sprim, Bprim), and well-behaved applications
/// won't call CreateRprim() (Sprim, Bprim) for prim types that aren't
/// supported. The core hydra prim types are "mesh", "basisCurves", and
/// "points", but a custom render delegate and a custom scene delegate could
/// add support for other prims such as implicit surfaces or volumes.
///
/// HdNSI Rprims create NSI geometry objects in the render delegate's
/// top-level NSI scene; and HdNSI's render pass draws by casting rays
/// into the top-level scene. The renderpass writes to the currently bound GL
/// framebuffer.
///
/// The render delegate also has a hook for the main hydra execution algorithm
/// (HdEngine::Execute()): between HdRenderIndex::SyncAll(), which pulls new
/// scene data, and execution of tasks, the engine calls back to
/// CommitResources(). This can be used to commit GPU buffers or, in HdNSI's
/// case, to do a final build of the BVH.
///
class HdNSIRenderDelegate final : public HdRenderDelegate {
public:
    /// Render delegate constructor. This method creates the NSI context and
    /// scene, and links NSI error handling to hydra error handling.
    HdNSIRenderDelegate(HdRenderSettingsMap const& settingsMap);
    /// Render delegate destructor. This method destroys the NSI context and
    /// scene.
    virtual ~HdNSIRenderDelegate();

    /// Return this delegate's render param.
    ///   \return A shared instance of HdNSIRenderParam.
    virtual HdRenderParam *GetRenderParam() const override;

    /// Return a list of which Rprim types can be created by this class's
    /// CreateRprim.
    virtual const TfTokenVector &GetSupportedRprimTypes() const override;
    /// Return a list of which Sprim types can be created by this class's
    /// CreateSprim.
    virtual const TfTokenVector &GetSupportedSprimTypes() const override;
    /// Return a list of which Bprim types can be created by this class's
    /// CreateBprim.
    virtual const TfTokenVector &GetSupportedBprimTypes() const override;

    /// Returns the HdResourceRegistry instance used by this render delegate.
    virtual HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    virtual VtDictionary GetRenderStats() const override;

    /// Create a renderpass. Hydra renderpasses are responsible for drawing
    /// a subset of the scene (specified by the "collection" parameter) to the
    /// current framebuffer. This class creates objects of type
    /// HdNSIRenderPass, which draw using NSI's raycasting API.
    ///   \param index The render index this renderpass will be bound to.
    ///   \param collection A specifier for which parts of the scene should
    ///                     be drawn.
    ///   \return An NSI renderpass object.
    virtual HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index,
                HdRprimCollection const& collection) override;

    /// Create an instancer. Hydra instancers store data needed for an
    /// instanced object to draw itself multiple times.
    ///   \param delegate The scene delegate providing data for this
    ///                   instancer.
    ///   \param id The scene graph ID of this instancer, used when pulling
    ///             data from a scene delegate.
    ///   \param instancerId If specified, the instancer at this id uses
    ///                      this instancer as a prototype.
    ///   \return An NSI instancer object.
    virtual HdInstancer *CreateInstancer(HdSceneDelegate *delegate,
                                         SdfPath const& id
                                         DECLARE_IID) override;

    /// Destroy an instancer created with CreateInstancer.
    ///   \param instancer The instancer to be destroyed.
    virtual void DestroyInstancer(HdInstancer *instancer) override;

    /// Create a hydra Rprim, representing scene geometry. This class creates
    /// NSI-specialized geometry containers like HdNSIMesh which map
    /// scene data to NSI scene graph objects.
    ///   \param typeId The rprim type to create. This must be one of the types
    ///                 from GetSupportedRprimTypes().
    ///   \param rprimId The scene graph ID of this rprim, used when pulling
    ///                  data from a scene delegate.
    ///   \param instancerId If specified, the instancer at this id uses the
    ///                      new rprim as a prototype.
    ///   \return An NSI rprim object.
    virtual HdRprim *CreateRprim(TfToken const& typeId,
                                 SdfPath const& rprimId
                                 DECLARE_IID) override;

    /// Destroy an Rprim created with CreateRprim.
    ///   \param rPrim The rprim to be destroyed.
    virtual void DestroyRprim(HdRprim *rPrim) override;

    /// Create a hydra Sprim, representing scene or viewport state like cameras
    /// or lights.
    ///   \param typeId The sprim type to create. This must be one of the types
    ///                 from GetSupportedSprimTypes().
    ///   \param sprimId The scene graph ID of this sprim, used when pulling
    ///                  data from a scene delegate.
    ///   \return An NSI sprim object.
    virtual HdSprim *CreateSprim(TfToken const& typeId,
                                 SdfPath const& sprimId) override;

    /// Create a hydra Sprim using default values, and with no scene graph
    /// binding.
    ///   \param typeId The sprim type to create. This must be one of the types
    ///                 from GetSupportedSprimTypes().
    ///   \return An NSI fallback sprim object.
    virtual HdSprim *CreateFallbackSprim(TfToken const& typeId) override;

    /// Destroy an Sprim created with CreateSprim or CreateFallbackSprim.
    ///   \param sPrim The sprim to be destroyed.
    virtual void DestroySprim(HdSprim *sPrim) override;

    /// Create a hydra Bprim, representing data buffers such as textures.
    ///   \param typeId The bprim type to create. This must be one of the types
    ///                 from GetSupportedBprimTypes().
    ///   \param bprimId The scene graph ID of this bprim, used when pulling
    ///                  data from a scene delegate.
    ///   \return An NSI bprim object.
    virtual HdBprim *CreateBprim(TfToken const& typeId,
                                 SdfPath const& bprimId) override;

    /// Create a hydra Bprim using default values, and with no scene graph
    /// binding.
    ///   \param typeId The bprim type to create. This must be one of the types
    ///                 from GetSupportedBprimTypes().
    ///   \return An NSI fallback bprim object.
    virtual HdBprim *CreateFallbackBprim(TfToken const& typeId) override;

    /// Destroy a Bprim created with CreateBprim or CreateFallbackBprim.
    ///   \param bPrim The bprim to be destroyed.
    virtual void DestroyBprim(HdBprim *bPrim) override;

    /// This function is called after new scene data is pulled during prim
    /// Sync(), but before any tasks (such as draw tasks) are run, and gives the
    /// render delegate a chance to transfer any invalidated resources to the
    /// rendering kernel. This class takes the  opportunity to update NSI's
    /// scene acceleration datastructures.
    ///   \param tracker The change tracker passed to prim Sync().
    virtual void CommitResources(HdChangeTracker *tracker) override;

    virtual TfToken GetMaterialBindingPurpose() const override;

    virtual TfToken GetMaterialNetworkSelector() const override;

    virtual TfTokenVector GetShaderSourceTypes() const override;

    virtual void SetRenderSetting(
        TfToken const& key, VtValue const& value) override;

    virtual HdRenderSettingDescriptorList
        GetRenderSettingDescriptors() const override;

    virtual HdAovDescriptor GetDefaultAovDescriptor(
        TfToken const& name) const override;

    void RemoveRenderPass(HdNSIRenderPass *renderPass);

    const std::string& GetDelight() const { return _delight; }

    const std::string FindShader(const std::string &id) const;

    DlShaderInfo* GetShaderInfo(const std::string &i_shader) const;

    DlShaderInfo* GetDefaultShader(
        const std::string &type,
        std::string *handle);

    const char* DefaultMaterialHandle() const { return "defaultShader"; }
    const char* DefaultSurfaceNode() const
        { return "defaultShader|PreviewSurface"; }

    bool IsBatch() const;
    bool HasAPIStreamProduct() const { return m_apistream_product; }

    void ProgressUpdate(const NSI::ProgressCallback::Value &i_progress);

private:
    void CreateNSIContext();

    void SetDisableLighting() const;
    void SetShadingSamples() const;
    void SetVolumeSamples() const;
    void SetMaxDiffuseDepth() const;
    void SetMaxReflectionDepth() const;
    void SetMaxRefractionDepth() const;
    void SetMaxHairDepth() const;
    void SetMaxDistance() const;
    void ExportDefaultMaterial() const;

private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

    /// Resource registry used in this render delegate
    static std::mutex _mutexResourceRegistry;
    static std::atomic_int _counterResourceRegistry;
    static HdResourceRegistrySharedPtr _resourceRegistry;

    // This class does not support copying.
    HdNSIRenderDelegate(const HdNSIRenderDelegate &)             = delete;
    HdNSIRenderDelegate &operator =(const HdNSIRenderDelegate &) = delete;

    // A shared NSI CAPI and context.
    std::unique_ptr<NSI::DynamicAPI> _capi;

    std::shared_ptr<NSI::Context> _nsi;

    bool m_apistream_product{false};

    // A shared HdNSIRenderParam object that stores top-level NSI state;
    // passed to prims during Sync().
    std::shared_ptr<HdNSIRenderParam> _renderParam;

    // Settings description for NSI renderer
    HdRenderSettingDescriptorList _settingDescriptors;

    /*
        Settings last communicated to renderer. Used to tell if updates are
        real updates because the settings dialog causes all settings to be set,
        even when the value does not change.
    */
    HdRenderSettingsMap _exportedSettings;

    /* All render pass objects created by this render delegate. */
    std::vector<HdNSIRenderPass*> _renderPasses;

    /* Root of renderer installation. */
    std::string _delight;

    /* Path to directory with our builtin shaders. */
    std::string _shaders_path;

    /* Pointer to dynamically loaded shader query API. */
    decltype(&DlGetShaderInfo) m_DlGetShaderInfo;

    /* List of shaders loaded for default connections. */
    std::vector<DlShaderInfo*> m_default_shaders;

    /* Rendering stats for GetRenderStats(). */
    mutable std::mutex m_render_stats_mutex;
    VtDictionary m_render_stats;

public:
    // A callback that interprets NSI error codes and injects them into
    // the hydra logging system.
    static void HandleNSIError(int level, int code, const char* msg);
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDNSI_RENDER_DELEGATE_H
// vim: set softtabstop=4 expandtab shiftwidth=4:
