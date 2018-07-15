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
#include "pxr/imaging/glf/glew.h"
#include "pxr/imaging/hdNSI/pointcloud.h"

#include "pxr/imaging/hdNSI/config.h"
#include "pxr/imaging/hdNSI/instancer.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/imaging/hdNSI/renderPass.h"
#include "pxr/imaging/hd/points.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/matrix4d.h"

#include <sstream>
#include <iostream>

#include <nsi.h>
#include <nsi.hpp>

PXR_NAMESPACE_OPEN_SCOPE

std::string HdNSIPointCloud::_nsiPointCloudDefaultGeoAttrHandle; // static
std::multimap<SdfPath, std::string> HdNSIPointCloud::_nsiPointCloudXformHandles; // static

HdNSIPointCloud::HdNSIPointCloud(SdfPath const& id,
                     SdfPath const& instancerId)
    : HdPoints(id, instancerId)
{
}

void
HdNSIPointCloud::Finalize(HdRenderParam *renderParam)
{
    NSIContext_t ctx = static_cast<HdNSIRenderParam*>(renderParam)
        ->AcquireSceneForEdit();
    NSI::Context nsi(ctx);

    const SdfPath &id = GetId();

    // Delete any instances of this pointcloud in the top-level NSI scene.
    auto range = _nsiPointCloudXformHandles.equal_range(id);
    for (auto itr = range.first; itr != range.second; ++itr) {
        const std::string &instanceXformHandle = itr->second;
        nsi.Delete(instanceXformHandle);
    }
    _nsiPointCloudXformHandles.erase(id);

    // Delete the prototype geometry.
    nsi.Delete(_masterShapeHandle);
    _masterShapeHandle.clear();

    //
    _nsiPointCloudDefaultGeoAttrHandle.clear();
}

HdDirtyBits
HdNSIPointCloud::_GetInitialDirtyBits() const
{
    // The initial dirty bits control what data is available on the first
    // run through _PopulateRtPointCloud(), so it should list every data item
    // that _PopulateRtPointCloud requests.
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::InitRepr
        | HdChangeTracker::DirtyPoints
        | HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyCullStyle
        | HdChangeTracker::DirtyDoubleSided
        | HdChangeTracker::DirtyRefineLevel
        | HdChangeTracker::DirtySubdivTags
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyNormals
        | HdChangeTracker::DirtyInstanceIndex
        ;

    return (HdDirtyBits)mask;
}

HdDirtyBits
HdNSIPointCloud::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
HdNSIPointCloud::_InitRepr(TfToken const &reprName,
                        HdDirtyBits *dirtyBits)
{
    TF_UNUSED(dirtyBits);

    // Create an empty repr.
    _ReprVector::iterator it = std::find_if(_reprs.begin(), _reprs.end(),
                                            _ReprComparator(reprName));
    if (it == _reprs.end()) {
        _reprs.emplace_back(reprName, HdReprSharedPtr());
    }
}

void
HdNSIPointCloud::_UpdateRepr(HdSceneDelegate *sceneDelegate,
                          TfToken const &reprName,
                          HdDirtyBits *dirtyBits)
{
    TF_UNUSED(sceneDelegate);
    TF_UNUSED(reprName);
    TF_UNUSED(dirtyBits);
    // NSI doesn't use the HdRepr structure.
}

void
HdNSIPointCloud::Sync(HdSceneDelegate* sceneDelegate,
                   HdRenderParam*   renderParam,
                   HdDirtyBits*     dirtyBits,
                   TfToken const&   reprName,
                   bool             forcedRepr)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // The repr token is used to look up an HdPointsReprDesc struct, which
    // has drawing settings for this prim to use. Repr opinions can come
    // from the render pass's rprim collection or the scene delegate;
    // _GetReprName resolves these multiple opinions.
    TfToken calculatedReprName = _GetReprName(reprName, forcedRepr);

    // XXX: PointCloudes can have multiple reprs; this is done, for example, when
    // the drawstyle specifies different rasterizing modes between front faces
    // and back faces. With raytracing, this concept makes less sense, but
    // combining semantics of two HdPointsReprDesc is tricky in the general case.
    // For now, HdNSIPointCloud only respects the first desc; this should be fixed.
    _PointsReprConfig::DescArray descs = _GetReprDesc(calculatedReprName);
    const HdPointsReprDesc &desc = descs[0];

    // Pull top-level NSI state out of the render param.
    HdNSIRenderParam *NSIRenderParam =
        static_cast<HdNSIRenderParam*>(renderParam);
    NSIContext_t ctx = NSIRenderParam->AcquireSceneForEdit();
    // NSIContext_t device = NSIRenderParam->GetNSIContext();

    // Create NSI geometry objects.
    _PopulateRtPointCloud(sceneDelegate, ctx, dirtyBits, desc);
}

void
HdNSIPointCloud::_CreateNSIPointCloud(NSIContext_t ctx)
{
    NSI::Context nsi(ctx);

    const SdfPath &id = GetId();
    _masterShapeHandle = id.GetString() + "|pointcloud1";

    // Create the new pointcloud.
    nsi.Create(_masterShapeHandle, "particles");

    // Create the master transform node.
    const std::string &masterXformHandle = id.GetString() + "|transform1";

    nsi.Create(masterXformHandle, "transform");
    nsi.Connect(masterXformHandle, "", NSI_SCENE_ROOT, "objects");
    nsi.Connect(_masterShapeHandle, "", masterXformHandle, "objects");

    _nsiPointCloudXformHandles.insert(std::make_pair(id, masterXformHandle));

    // Create the default shader.
    if (_nsiPointCloudDefaultGeoAttrHandle.empty()) {
        _nsiPointCloudDefaultGeoAttrHandle = "nsiPointCloudDefaultGeoAttr1";
        nsi.Create(_nsiPointCloudDefaultGeoAttrHandle, "attributes");

        const HdNSIConfig &config = HdNSIConfig::GetInstance();
        const std::string &defaultShaderPath =
            config.delight + "/maya/osl/dl3DelightMaterial";

        const std::string defaultShaderHandle("nsiPointCloudDefaultShader1");
        nsi.Create(defaultShaderHandle, "shader");
        nsi.SetAttribute(defaultShaderHandle,
            (NSI::StringArg("shaderfilename", defaultShaderPath),
                NSI::FloatArg("i_color", 0.6)));

        nsi.Connect(defaultShaderHandle, "",
            _nsiPointCloudDefaultGeoAttrHandle, "surfaceshader");
    }

    nsi.Connect(_nsiPointCloudDefaultGeoAttrHandle, "", masterXformHandle, "geometryattributes");
}

void
HdNSIPointCloud::_SetNSIPointCloudAttributes(NSIContext_t ctx)
{
    NSI::Context nsi(ctx);

    NSI::ArgumentList attrs;

    // "P"
    attrs.push(NSI::Argument::New("P")
        ->SetType(NSITypePoint)
        ->SetCount(_points.size())
        ->SetValuePointer(_points.cdata()));

   // "width"
    attrs.push(NSI::Argument::New("width")
        ->SetType(NSITypeFloat)
        ->SetCount(_widths.size())
        ->SetValuePointer(_widths.data()));

    // "id"
    attrs.push(NSI::Argument::New("id")
        ->SetType(NSITypeInteger)
        ->SetCount(_pointsIds.size())
        ->SetValuePointer(_pointsIds.data()));

    // "N"
    if (_normals.size()) {
        attrs.push(NSI::Argument::New("N")
            ->SetType(NSITypeNormal)
            ->SetCount(_normals.size())
            ->SetValuePointer(_normals.cdata()));
    }

    nsi.SetAttribute(_masterShapeHandle, attrs);
}

void
HdNSIPointCloud::_UpdatePrimvarSources(HdSceneDelegate* sceneDelegate,
                                    HdDirtyBits dirtyBits)
{
    HD_TRACE_FUNCTION();
    SdfPath const& id = GetId();

    // Update _primvarSourceMap, our local cache of raw primvar data.
    // This function pulls data from the scene delegate, but defers processing.
    //
    // While iterating primvars, we skip "points" (vertex positions) because
    // the points primvar is processed by _PopulateRtPointCloud. We only call
    // GetPrimvar on primvars that have been marked dirty.
    //
    // Currently, hydra doesn't have a good way of communicating changes in
    // the set of primvars, so we only ever add and update to the primvar set.

    HdPrimvarDescriptorVector primvars;
    for (size_t i=0; i < HdInterpolationCount; ++i) {
        HdInterpolation interp = static_cast<HdInterpolation>(i);
        primvars = GetPrimvarDescriptors(sceneDelegate, interp);
        for (HdPrimvarDescriptor const& pv: primvars) {
            if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name) &&
                pv.name != HdTokens->points) {
                _primvarSourceMap[pv.name] = {
                    GetPrimvar(sceneDelegate, pv.name),
                    interp
                };
            }
        }
    }
}

void
HdNSIPointCloud::_PopulateRtPointCloud(HdSceneDelegate* sceneDelegate,
                              NSIContext_t           ctx,
                              HdDirtyBits*     dirtyBits,
                              HdPointsReprDesc const &desc)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();

    bool newPointCloud = false;

    ////////////////////////////////////////////////////////////////////////
    // 1. Pull scene data.

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        VtValue value = sceneDelegate->Get(id, HdTokens->points);
        _points = value.Get<VtVec3fArray>();

        // XXX TODO: get widths from scene data (now are all set to same value: 1.0)
        _pointsIds.clear();
        _widths.clear();
        int pId = 0;
        for (const auto& pt : _points) {
            (void)pt;
            _pointsIds.push_back(pId++);
            _widths.push_back(1.0f);
        }

        newPointCloud = true;
    }

    // XXX TODO: normals (for discs) ?

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _transform = sceneDelegate->GetTransform(id);
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        _UpdateVisibility(sceneDelegate, dirtyBits);
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals) ||
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths) ||
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->primvar)) {
        _UpdatePrimvarSources(sceneDelegate, *dirtyBits);
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths) ||
        (_primvarSourceMap.count(HdTokens->widths) > 0)) {
        // auto v_widths = sceneDelegate->Get(id, HdTokens->widths);
        auto v_widths = _primvarSourceMap[HdTokens->widths].data;
        // std::cout << "### POINTCLOUD " << id << " WIDTHS (VtValue):" << v_widths << std::endl;
        if (! v_widths.IsEmpty()) {
            auto widths = v_widths.Get<VtArray<float>>();
            // std::cout << "### POINTCLOUD " << id << " WIDTHS (VtArray):" << widths << std::endl;

            if (widths.size() > 0) {
                _widths.clear();
                _widths = widths;
            }
        }
        // std::cout << "### POINTCLOUD " << id << " FINAL WIDTHS:" << _widths << std::endl;
    }

    ////////////////////////////////////////////////////////////////////////
    // 2. Resolve drawstyles

    // XXX TODO: discs ?
    // bool doDiscs = (desc.geomStyle == HdPointsGeomStyleDiscs);

    ////////////////////////////////////////////////////////////////////////
    // 3. Populate NSI prototype object.

    if (newPointCloud) {
        // Create the new pointcloud node.
        _CreateNSIPointCloud(ctx);

        // attributes will be (re-)populated below.
    }

    // Populate/update points in the NSI pointcloud.
    if (newPointCloud || 
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points) ||
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths) ||
        (_primvarSourceMap.count(HdTokens->widths) > 0)) {
        _SetNSIPointCloudAttributes(ctx);
    }

    ////////////////////////////////////////////////////////////////////////
    // 4. Populate NSI instance objects.

    // If the pointcloud is instanced, create one new instance per transform.
    // XXX: The current instancer invalidation tracking makes it hard for
    // HdNSI to tell whether transforms will be dirty, so this code
    // pulls them every frame.
    if (!GetInstancerId().IsEmpty()) {
        NSI::Context nsi(ctx);

        // Retrieve instance transforms from the instancer.
        HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
        HdInstancer *instancer =
            renderIndex.GetInstancer(GetInstancerId());
        const VtMatrix4dArray &transforms =
            static_cast<HdNSIInstancer*>(instancer)->
            ComputeInstanceTransforms(GetId());

        // Retrieve the all existed transforms.
        std::set<std::string> existedXformHandles;
        const auto range = _nsiPointCloudXformHandles.equal_range(id);
        for (auto itr = range.first; itr != range.second; ++itr) {
            existedXformHandles.insert(itr->second);
        }

        // Create and update the instance transform nodes.
        for (size_t i = 0; i < transforms.size(); ++i ) {
            std::ostringstream instanceXformHandleStream;
            instanceXformHandleStream << id << "|instancetransform" << i;
            const std::string &instanceXformHandle =
                instanceXformHandleStream.str();

            if (!existedXformHandles.count(instanceXformHandle)) {
                nsi.Create(instanceXformHandle, "transform");
                nsi.Connect(instanceXformHandle, "", NSI_SCENE_ROOT, "objects");
                nsi.Connect(_masterShapeHandle, "", instanceXformHandle, "objects");
                nsi.Connect(_nsiPointCloudDefaultGeoAttrHandle, "",
                    instanceXformHandle, "geometryattributes");
            }

            nsi.SetAttributeAtTime(instanceXformHandle, 0,
                NSI::DoubleMatrixArg("transformationmatrix", transforms[i].GetArray()));
        }
    }
    // Otherwise, create our single instance (if necessary) and update
    // the transform (if necessary).
    else {
        // Check if we have the master transform.
        bool hasXform = (_nsiPointCloudXformHandles.count(id) > 0);
        if (!hasXform) {
            std::string masterXformHandle = id.GetString() + "|transform1";

            NSI::Context nsi(ctx);
            nsi.Create(masterXformHandle, "transform");
            nsi.Connect(masterXformHandle, "", NSI_SCENE_ROOT, "objects");
            nsi.Connect(_masterShapeHandle, "", masterXformHandle, "objects");
        }

        if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
            NSI::Context nsi(ctx);

            // Update the transform.
            std::string masterXformHandle = id.GetString() + "|transform1";

            nsi.SetAttributeAtTime(masterXformHandle, 0.0,
                NSI::DoubleMatrixArg("transformationmatrix", _transform.GetArray()));
        }
    }

    // Clean all dirty bits.
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
