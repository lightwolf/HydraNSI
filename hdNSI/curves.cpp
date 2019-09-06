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
#include "pxr/imaging/hdNSI/curves.h"

#include "pxr/imaging/hdNSI/instancer.h"
#include "pxr/imaging/hdNSI/renderDelegate.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/imaging/hdNSI/renderPass.h"
#include "pxr/imaging/hd/basisCurves.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/matrix4d.h"

#include <sstream>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

std::map<SdfPath, std::string> HdNSICurves::_nsiCurvesShapeHandles; // static
std::multimap<SdfPath, std::string> HdNSICurves::_nsiCurvesXformHandles; // static

HdNSICurves::HdNSICurves(SdfPath const& id,
                     SdfPath const& instancerId)
    : HdBasisCurves(id, instancerId)
    , _basis(HdTokens->catmullRom)
    , _refined(false)
{
}

void
HdNSICurves::Finalize(HdRenderParam *renderParam)
{
    NSI::Context &nsi =
        static_cast<HdNSIRenderParam*>(renderParam)->AcquireSceneForEdit();

    const SdfPath &id = GetId();

    // Delete any instances of this curves in the top-level NSI scene.
    if (_nsiCurvesXformHandles.count(id)) {
        auto range = _nsiCurvesXformHandles.equal_range(id);
        for (auto itr = range.first; itr != range.second; ++ itr) {
            const std::string &handle = itr->second;
            nsi.Delete(handle);
        }
    }
    _nsiCurvesXformHandles.erase(id);

    // Delete the attribute.
    nsi.Delete(_attrsHandle);

    _attrsHandle.clear();

    // Delete the geometry.
    if (_nsiCurvesShapeHandles.count(id)) {
        _nsiCurvesShapeHandles.erase(id);
        nsi.Delete(_masterShapeHandle);
    }

    _masterShapeHandle.clear();
}

HdDirtyBits
HdNSICurves::GetInitialDirtyBitsMask() const
{
    // The initial dirty bits control what data is available on the first
    // run through _PopulateRtCurves(), so it should list every data item
    // that _PopulateRtCurves requests.
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::InitRepr
        | HdChangeTracker::DirtyPrimID
        | HdChangeTracker::DirtyPoints
        | HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyCullStyle
        | HdChangeTracker::DirtyDoubleSided
        | HdChangeTracker::DirtyDisplayStyle
        | HdChangeTracker::DirtySubdivTags
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyNormals
        | HdChangeTracker::DirtyInstanceIndex
        | HdChangeTracker::DirtyMaterialId
        ;

    return (HdDirtyBits)mask;
}

HdDirtyBits
HdNSICurves::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
HdNSICurves::_InitRepr(TfToken const &reprName,
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
HdNSICurves::Sync(HdSceneDelegate* sceneDelegate,
                   HdRenderParam*   renderParam,
                   HdDirtyBits*     dirtyBits,
                   TfToken const&   reprName)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // XXX: Curveses can have multiple reprs; this is done, for example, when
    // the drawstyle specifies different rasterizing modes between front faces
    // and back faces. With raytracing, this concept makes less sense, but
    // combining semantics of two HdBasisCurvesReprDesc is tricky in the general case.
    // For now, HdNSICurves only respects the first desc; this should be fixed.
    _BasisCurvesReprConfig::DescArray descs = _GetReprDesc(reprName);
    const HdBasisCurvesReprDesc &desc = descs[0];

    // Pull top-level NSI state out of the render param.
    auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
    NSI::Context &nsi = nsiRenderParam->AcquireSceneForEdit();

    // Create NSI geometry objects.
    _PopulateRtCurves(sceneDelegate, nsiRenderParam, nsi, dirtyBits, desc);
}

void
HdNSICurves::_CreateNSICurves(
    HdNSIRenderParam *renderParam,
    NSI::Context &nsi)
{
    const SdfPath &id = GetId();
    _masterShapeHandle = id.GetString() + "|curves1";

    // Create the new curves.
    // Scan the all CVC to decice the degree.
    int minCvc = 999;
    for (int cvc : _curveVertexCounts) {
        minCvc = std::min(minCvc, cvc);
    }
    nsi.Create(_masterShapeHandle, minCvc >= 4 ? "cubiccurves" : "linearcurves");

    // Create the master transform node.
    const std::string &masterXformHandle = id.GetString() + "|transform1";

    nsi.Create(masterXformHandle, "transform");
    nsi.Connect(masterXformHandle, "", NSI_SCENE_ROOT, "objects");
    nsi.Connect(_masterShapeHandle, "", masterXformHandle, "objects");

    _nsiCurvesXformHandles.insert(std::make_pair(id, masterXformHandle));

    // Create the attribute node and connect shader to this.
    _attrsHandle = id.GetString() + "|attributes1";

    nsi.Create(_attrsHandle, "attributes");

    nsi.Connect(_attrsHandle, "", masterXformHandle, "geometryattributes");
}

void
HdNSICurves::_SetNSICurvesAttributes(NSI::Context &nsi)
{
    NSI::ArgumentList attrs;

    // "nvertices"
    attrs.push(NSI::Argument::New("nvertices")
        ->SetType(NSITypeInteger)
        ->SetCount(_curveVertexCounts.size())
        ->SetValuePointer(_curveVertexCounts.cdata()));

    // "P"
    attrs.push(NSI::Argument::New("P")
        ->SetType(NSITypePoint)
        ->SetCount(_points.size())
        ->SetValuePointer(_points.cdata()));

   // "width"
    attrs.push(NSI::Argument::New("width")
        ->SetType(NSITypeFloat)
        ->SetCount(_widths.size())
        ->SetValuePointer(_widths.cdata()));

    // "basis"
    if (_basis == HdTokens->catmullRom)
        attrs.push(new NSI::StringArg("basis", "catmull-rom"));
    else if (_basis == HdTokens->bSpline)
        attrs.push(new NSI::StringArg("basis", "b-spline"));
    else {
        // HdTokens->bezier is not supported!
        // use a spline as a fallback
        attrs.push(new NSI::StringArg("basis", "b-spline"));
    }

    nsi.SetAttribute(_masterShapeHandle, attrs);
}

void
HdNSICurves::_PopulateRtCurves(HdSceneDelegate* sceneDelegate,
                               HdNSIRenderParam *renderParam,
                               NSI::Context &nsi,
                               HdDirtyBits* dirtyBits,
                               HdBasisCurvesReprDesc const &desc)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();

    bool newCurves = false;

    ////////////////////////////////////////////////////////////////////////
    // 1. Pull scene data.

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        // Get the all points.
        VtValue pointsVal = sceneDelegate->Get(id, HdTokens->points);
        _points = pointsVal.Get<VtVec3fArray>();

        // Get the widths.
        _widths.clear();

        VtValue widthsVal = GetPrimvar(sceneDelegate, HdTokens->widths);
        if (widthsVal.IsEmpty()) {
            _widths.resize(_points.size());
            std::fill(_widths.begin(), _widths.end(), 0.1f);
        } else {
            _widths = widthsVal.Get<VtFloatArray>();
        }

        newCurves = true;
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        const HdBasisCurvesTopology &srcTopology = GetBasisCurvesTopology(sceneDelegate);
        _topology = HdBasisCurvesTopology(srcTopology.GetCurveType(),
                          srcTopology.GetCurveBasis(),
                          srcTopology.GetCurveWrap(),
                          srcTopology.GetCurveVertexCounts(),
                          srcTopology.GetCurveIndices());

        _curveVertexCounts = _topology.GetCurveVertexCounts();
        _curveVertexIndices = _topology.GetCurveIndices();
        _basis = _topology.GetCurveBasis();

        newCurves = true;
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _transform = sceneDelegate->GetTransform(id);
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        _UpdateVisibility(sceneDelegate, dirtyBits);
    }

    ////////////////////////////////////////////////////////////////////////
    // 2. Resolve drawstyles

    int refineLevel = GetDisplayStyle(sceneDelegate).refineLevel;
    bool doRefine = (refineLevel > 0);

    ////////////////////////////////////////////////////////////////////////
    // 3. Populate NSI prototype object.

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id) ||
        doRefine != _refined) {

        newCurves = true;

        // Create the new curves node.
        _CreateNSICurves(renderParam, nsi);

        _refined = doRefine;
        // In both cases, vertices/attributes will be (re-)populated below.
    }

    // Populate/update points in the NSI curves.
    if (newCurves || 
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        _SetNSICurvesAttributes(nsi);
    }

    // Update visibility.
    //
    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        nsi.SetAttribute(_attrsHandle, (NSI::IntegerArg("visibility", _sharedData.visible ? 1 : 0),
            NSI::IntegerArg("visibility.priority", 1)));
    }

    ////////////////////////////////////////////////////////////////////////
    // 4. Populate NSI instance objects.

    // If the curves is instanced, create one new instance per transform.
    // XXX: The current instancer invalidation tracking makes it hard for
    // HdNSI to tell whether transforms will be dirty, so this code
    // pulls them every frame.
    if (!GetInstancerId().IsEmpty()) {
        // Retrieve instance transforms from the instancer.
        HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
        HdInstancer *instancer =
            renderIndex.GetInstancer(GetInstancerId());
        const VtMatrix4dArray &transforms =
            static_cast<HdNSIInstancer*>(instancer)->
            ComputeInstanceTransforms(GetId());

        // Retrieve the all existed transforms.
        std::set<std::string> existedXformHandles;
        const auto range = _nsiCurvesXformHandles.equal_range(id);
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
                nsi.Connect(_attrsHandle, "", instanceXformHandle, "geometryattributes");
            }

            nsi.SetAttributeAtTime(instanceXformHandle, 0,
                NSI::DoubleMatrixArg("transformationmatrix", transforms[i].GetArray()));
        }
    }
    // Otherwise, create our single instance (if necessary) and update
    // the transform (if necessary).
    else {
        // Check if we have the master transform.
        bool hasXform = (_nsiCurvesXformHandles.count(id) > 0);
        if (!hasXform) {
            // Create the master transform.
            std::string masterXformHandle = id.GetString() + "|transform1";

            nsi.Create(masterXformHandle, "transform");
            nsi.Connect(masterXformHandle, "", NSI_SCENE_ROOT, "objects");
            nsi.Connect(_masterShapeHandle, "", masterXformHandle, "objects");
        }

        if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
            // Update the transform.
            std::string masterXformHandle = id.GetString() + "|transform1";

            nsi.SetAttributeAtTime(masterXformHandle, 0.0,
                NSI::DoubleMatrixArg("transformationmatrix", _transform.GetArray()));
        }
    }

    if (HdChangeTracker::IsPrimIdDirty(*dirtyBits, id))
    {
        nsi.SetAttribute(_masterShapeHandle,
            NSI::IntegerArg("primId", GetPrimId()));
    }

    _material.Sync(
        sceneDelegate, renderParam, dirtyBits, nsi, GetId(),
        _masterShapeHandle);

    _primvars.Sync(
        sceneDelegate, renderParam, dirtyBits, nsi, GetId(),
        _masterShapeHandle, VtIntArray()); // _curveVertexIndices ?

    // Clean all dirty bits.
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
