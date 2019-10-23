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
#include "pxr/imaging/hdNSI/mesh.h"

#include "pxr/imaging/hdNSI/renderDelegate.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/imaging/hdNSI/renderPass.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/smoothNormals.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/matrix4d.h"

#include <sstream>
#include <iostream>

#include <nsi.hpp>

PXR_NAMESPACE_OPEN_SCOPE

HdNSIMesh::HdNSIMesh(SdfPath const& id,
                     SdfPath const& instancerId)
    : HdMesh(id, instancerId)
    , _normalsInterpolation(HdInterpolationFaceVarying)
    , _adjacencyValid(false)
    , _computedNormalsValid(false)
    , _authoredNormals(false)
    , _smoothNormals(false)
    , _base{"mesh"}
{
}

void
HdNSIMesh::Finalize(HdRenderParam *renderParam)
{
    _base.Finalize(static_cast<HdNSIRenderParam*>(renderParam));
}

HdDirtyBits
HdNSIMesh::GetInitialDirtyBitsMask() const
{
    // The initial dirty bits control what data is available on the first
    // run through _PopulateRtMesh(), so it should list every data item
    // that _PopulateRtMesh requests.
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
HdNSIMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
HdNSIMesh::_InitRepr(TfToken const &reprName,
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
HdNSIMesh::Sync(HdSceneDelegate* sceneDelegate,
                 HdRenderParam*   renderParam,
                 HdDirtyBits*     dirtyBits,
                 TfToken const&   reprName)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // XXX: Meshes can have multiple reprs; this is done, for example, when
    // the drawstyle specifies different rasterizing modes between front faces
    // and back faces. With raytracing, this concept makes less sense, but
    // combining semantics of two HdMeshReprDesc is tricky in the general case.
    // For now, HdNSIMesh only respects the first desc; this should be fixed.
    _MeshReprConfig::DescArray descs = _GetReprDesc(reprName);
    const HdMeshReprDesc &desc = descs[0];

    // Pull top-level NSI state out of the render param.
    auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
    NSI::Context &nsi = nsiRenderParam->AcquireSceneForEdit();

    /* The base rprim class tracks this but does not update it itself. */
    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, GetId()))
    {
        _UpdateVisibility(sceneDelegate, dirtyBits);
    }

    /* This creates the NSI nodes so it comes before other attributes. */
    _base.Sync(sceneDelegate, nsiRenderParam, dirtyBits, *this);

    // Create NSI geometry objects.
    _PopulateRtMesh(sceneDelegate, nsiRenderParam, nsi, dirtyBits, desc);
}

void
HdNSIMesh::_SetNSIMeshAttributes(NSI::Context &nsi)
{
    NSI::ArgumentList attrs;

    // "nvertices"
    attrs.push(NSI::Argument::New("nvertices")
        ->SetType(NSITypeInteger)
        ->SetCount(_faceVertexCounts.size())
        ->SetValuePointer(_faceVertexCounts.cdata()));

    // "P"
    attrs.push(NSI::Argument::New("P")
        ->SetType(NSITypePoint)
        ->SetCount(_points.size())
        ->SetValuePointer(_points.cdata()));

    // "P.indices"
    attrs.push(NSI::Argument::New("P.indices")
        ->SetType(NSITypeInteger)
        ->SetCount(_faceVertexIndices.size())
        ->SetValuePointer(_faceVertexIndices.cdata()));

    // "N"
    if (_normals.size()) {
        attrs.push(NSI::Argument::New("N")
            ->SetType(NSITypeNormal)
            ->SetCount(_normals.size())
            ->SetValuePointer(_normals.cdata()));

        if (_normalsInterpolation == HdInterpolationVertex)
        {
            attrs.push(NSI::Argument::New("N.indices")
                ->SetType(NSITypeInteger)
                ->SetCount(_faceVertexIndices.size())
                ->SetValuePointer(_faceVertexIndices.cdata()));
        }
    }

    nsi.SetAttribute(_base.Shape(), attrs);
}

void
HdNSIMesh::_PopulateRtMesh(HdSceneDelegate* sceneDelegate,
                           HdNSIRenderParam *renderParam,
                           NSI::Context &nsi,
                           HdDirtyBits* dirtyBits,
                           HdMeshReprDesc const &desc)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();

    ////////////////////////////////////////////////////////////////////////
    // 1. Pull scene data.

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        VtValue pointsValue = sceneDelegate->Get(id, HdTokens->points);
        _points = pointsValue.Get<VtVec3fArray>();
        _computedNormalsValid = false;
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id))
    {
        /*
            Note that the refine level comes from
            HdSceneDelegate::GetDisplayStyle() and the subdiv tags from
            HdSceneDelegate::GetSubdivTags(). They both have their own dirty
            bits. So the value we get here with topology should not be used.
        */
        _topology = GetMeshTopology(sceneDelegate);

        _faceVertexCounts = _topology.GetFaceVertexCounts();
        _faceVertexIndices = _topology.GetFaceVertexIndices();
        _adjacencyValid = false;
        _computedNormalsValid = false;

        /* Set winding order. */
        nsi.SetAttribute(_base.Shape(), NSI::IntegerArg("clockwisewinding",
            _topology.GetOrientation() == HdTokens->leftHanded ? 1 : 0));

        /* Enable (or not) subdivision. */
        bool subdiv =
            _topology.GetScheme() == PxOsdOpenSubdivTokens->catmullClark;
        nsi.SetAttribute(_base.Shape(), NSI::CStringPArg("subdivision.scheme",
                subdiv ? "catmull-clark" : ""));
    }

    if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id))
    {
        NSI::ArgumentList attrs;
        PxOsdSubdivTags subdivTags = sceneDelegate->GetSubdivTags(id);

        const VtIntArray &cornerIndices = subdivTags.GetCornerIndices();
        const VtFloatArray &cornerSharpness = subdivTags.GetCornerWeights();
        if (!cornerIndices.empty() && !cornerSharpness.empty())
        {
            attrs.push(NSI::Argument::New("subdivision.cornervertices")
                ->SetType(NSITypeInteger)
                ->SetCount(cornerIndices.size())
                ->SetValuePointer(cornerIndices.data()));
            attrs.push(NSI::Argument::New("subdivision.cornersharpness")
                ->SetType(NSITypeFloat)
                ->SetCount(cornerSharpness.size())
                ->SetValuePointer(cornerSharpness.data()));
        }

        const VtIntArray &creaseIndices = subdivTags.GetCreaseIndices();
        const VtFloatArray &creaseSharpness = subdivTags.GetCreaseWeights();
        if (!creaseIndices.empty() && !creaseSharpness.empty())
        {
            attrs.push(NSI::Argument::New("subdivision.creasevertices")
                ->SetType(NSITypeInteger)
                ->SetCount(creaseIndices.size())
                ->SetValuePointer(creaseIndices.data()));
            attrs.push(NSI::Argument::New("subdivision.creasesharpness")
                ->SetType(NSITypeFloat)
                ->SetCount(creaseSharpness.size())
                ->SetValuePointer(creaseSharpness.data()));
        }

        if (!attrs.empty())
        {
            nsi.SetAttribute(_base.Shape(), attrs);
        }
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals)) {
        // TODO: support other interpolations than FaceVarying (ie, uniform, vertex)

        // If we just do "GetNormals" when there are no normals,  it will
        // raise a scary error... but there's no "easy" way to check if normals
        // are defined - so we use GetPrimvarDescriptors, and iterate through all
        auto primvars = GetPrimvarDescriptors(sceneDelegate, HdInterpolationFaceVarying);
        for (HdPrimvarDescriptor const& pv: primvars) {
            if (pv.name == HdTokens->normals) {
                VtValue normalsValue = GetNormals(sceneDelegate);
                if (normalsValue.IsHolding<VtVec3fArray>()) {
                    _normals = normalsValue.UncheckedGet<VtVec3fArray>();
                    _authoredNormals = true;
                    _normalsInterpolation = pv.interpolation;
                }
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////
    // 2. Resolve drawstyles

    // The repr defines whether we should compute smooth normals for this mesh:
    // per-vertex normals taken as an average of adjacent faces, and
    // interpolated smoothly across faces.
    _smoothNormals = !desc.flatShadingEnabled;

    // If the subdivision scheme is "none" or "bilinear", force us not to use
    // smooth normals.
#if 0
    _smoothNormals = _smoothNormals &&
        (_topology.GetScheme() != PxOsdOpenSubdivTokens->none) &&
        (_topology.GetScheme() != PxOsdOpenSubdivTokens->bilinear);
#endif
    /* Don't compute smooth normals on a subdiv. They are implicitly smooth. */
    _smoothNormals = _smoothNormals &&
        _topology.GetScheme() != PxOsdOpenSubdivTokens->catmullClark;

    ////////////////////////////////////////////////////////////////////////
    // 3. Populate NSI prototype object.

    // Update normals
    if (!_authoredNormals && _smoothNormals) {
        _normalsInterpolation = HdInterpolationVertex;
        // Update the smooth normals in steps:
       // 1. If the topology is dirty, update the adjacency table, a processed
       //    form of the topology that helps calculate smooth normals quickly.
       // 2. If the points are dirty, update the smooth normal buffer itself.
        if (!_adjacencyValid) {
            _adjacency.BuildAdjacencyTable(&_topology);
            _adjacencyValid = true;
            // If we rebuilt the adjacency table, force a rebuild of normals.
            _computedNormalsValid = false;
        }
        if (!_computedNormalsValid) {
            _normals = Hd_SmoothNormals::ComputeSmoothNormals(
                &_adjacency, _points.size(), _points.cdata());
            _computedNormalsValid = true;
        }
    }
    // If we have no authored normals, but we're not doing smooth normals, we just let
    // 3delight use it's own default normals

    // Populate/update points in the NSI mesh.
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        _SetNSIMeshAttributes(nsi);
    }

    _material.Sync(
        sceneDelegate, renderParam, dirtyBits, nsi, GetId(),
        _base.Shape());

    _primvars.Sync(
        sceneDelegate, renderParam, dirtyBits, nsi, GetId(),
        _base.Shape(), _faceVertexIndices);

    // Clean all dirty bits.
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
