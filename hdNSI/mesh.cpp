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

#include "pxr/imaging/hdNSI/instancer.h"
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

std::map<SdfPath, std::string> HdNSIMesh::_nsiMeshShapeHandles; // static

std::multimap<SdfPath, std::string> HdNSIMesh::_nsiMeshXformHandles; // static

HdNSIMesh::HdNSIMesh(SdfPath const& id,
                     SdfPath const& instancerId)
    : HdMesh(id, instancerId)
    , _normalsInterpolation(HdInterpolationFaceVarying)
    , _adjacencyValid(false)
    , _computedNormalsValid(false)
    , _authoredNormals(false)
    , _smoothNormals(false)
    , _leftHanded(-1)
    , _refined(false)
    , _doubleSided(false)
    , _cullStyle(HdCullStyleDontCare)
{
}

void
HdNSIMesh::Finalize(HdRenderParam *renderParam)
{
    NSI::Context &nsi =
        static_cast<HdNSIRenderParam*>(renderParam)->AcquireSceneForEdit();

    const SdfPath &id = GetId();

    // Delete any instances of this mesh in the top-level NSI scene.
    auto range = _nsiMeshXformHandles.equal_range(id);
    for (auto itr = range.first; itr != range.second; ++ itr) {
        const std::string &instanceXformHandle = itr->second;
        nsi.Delete(instanceXformHandle);
    }
    _nsiMeshXformHandles.erase(id);

    // Delete the prototype geometry.
    if (_nsiMeshShapeHandles.count(id)) {
        _nsiMeshShapeHandles.erase(id);
        nsi.Delete(_masterShapeHandle);
    }

    _masterShapeHandle.clear();

    // Delete the attributes node.
    nsi.Delete(_attrsHandle);

    _attrsHandle.clear();
}

HdDirtyBits
HdNSIMesh::GetInitialDirtyBitsMask() const
{
    // The initial dirty bits control what data is available on the first
    // run through _PopulateRtMesh(), so it should list every data item
    // that _PopulateRtMesh requests.
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::InitRepr
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

    // Create NSI geometry objects.
    _PopulateRtMesh(sceneDelegate, nsiRenderParam, nsi, dirtyBits, desc);
}

bool
HdNSIMesh::_CreateNSIMesh(
    HdNSIRenderParam *renderParam,
    NSI::Context &nsi)
{
    const SdfPath &id = GetId();
    _masterShapeHandle = id.GetString() + "|mesh1";

    // Create the new mesh.
    bool newShape = false;

    if (!_nsiMeshShapeHandles.count(id)) {
        nsi.Create(_masterShapeHandle, "mesh");
        _nsiMeshShapeHandles.insert(std::make_pair(id, _masterShapeHandle));

        newShape = true;
    }

    // Set clockwisewinding for the mesh.
    if (_leftHanded != -1) {
        nsi.SetAttribute(_masterShapeHandle,
            NSI::IntegerArg("clockwisewinding", _leftHanded));
    }

    // Create the master transform node.
    const std::string &masterXformHandle = id.GetString() + "|transform1";

    nsi.Create(masterXformHandle, "transform");
    nsi.Connect(masterXformHandle, "", NSI_SCENE_ROOT, "objects");
    nsi.Connect(_masterShapeHandle, "", masterXformHandle, "objects");

    _nsiMeshXformHandles.insert(std::make_pair(id, masterXformHandle));

    // Create the attribute node.
    _attrsHandle = id.GetString() + "|attributes1";

    nsi.Create(_attrsHandle, "attributes");
    nsi.Connect(_attrsHandle, "", masterXformHandle, "geometryattributes");

    return newShape;
}

void
HdNSIMesh::_SetNSIMeshAttributes(NSI::Context &nsi, bool asSubdiv)
{
    NSI::ArgumentList attrs;

    // Set if this mesh is subdivision.
    const TfToken &scheme = _topology.GetScheme();
    asSubdiv |= (scheme == PxOsdOpenSubdivTokens->catmullClark);

    if (asSubdiv) {
        nsi.SetAttribute(_masterShapeHandle,
            NSI::CStringPArg("subdivision.scheme", "catmull-clark"));
    }

    // Subdivision-related attributes.
    VtIntArray cornerIndices;
    VtFloatArray cornerSharpness;

    VtIntArray creaseIndices;
    VtFloatArray creaseSharpness;

    if (asSubdiv) {
        const PxOsdSubdivTags &subdivTags = _topology.GetSubdivTags();

        cornerIndices = subdivTags.GetCornerIndices();
        cornerSharpness = subdivTags.GetCornerWeights();
        if (cornerIndices.size() && cornerSharpness.size()) {
            attrs.push(NSI::Argument::New("subdivision.cornervertices")
                ->SetType(NSITypeInteger)
                ->SetCount(cornerIndices.size())
                ->SetValuePointer(cornerIndices.data()));

            attrs.push(NSI::Argument::New("subdivision.cornersharpness")
                ->SetType(NSITypeFloat)
                ->SetCount(cornerSharpness.size())
                ->SetValuePointer(cornerSharpness.data()));
        }

        creaseIndices = subdivTags.GetCreaseIndices();
        creaseSharpness = subdivTags.GetCreaseWeights();
        if (creaseIndices.size() && creaseSharpness.size()) {
            attrs.push(NSI::Argument::New("subdivision.creasevertices")
                ->SetType(NSITypeInteger)
                ->SetCount(creaseIndices.size())
                ->SetValuePointer(creaseIndices.data()));

            attrs.push(NSI::Argument::New("subdivision.creasesharpness")
                ->SetType(NSITypeFloat)
                ->SetCount(creaseSharpness.size())
                ->SetValuePointer(creaseSharpness.data()));
        }
    }

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

    nsi.SetAttribute(_masterShapeHandle, attrs);
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

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        // When pulling a new topology, we don't want to overwrite the
        // refine level or subdiv tags, which are provided separately by the
        // scene delegate, so we save and restore them.
        PxOsdSubdivTags subdivTags = _topology.GetSubdivTags();
        int refineLevel = _topology.GetRefineLevel();
        _topology = HdMeshTopology(GetMeshTopology(sceneDelegate), refineLevel);
        _topology.SetSubdivTags(subdivTags);

        _faceVertexCounts = _topology.GetFaceVertexCounts();
        _faceVertexIndices = _topology.GetFaceVertexIndices();
        _adjacencyValid = false;
        _computedNormalsValid = false;

        if (_topology.GetOrientation() == HdTokens->leftHanded) {
            _leftHanded = 1;
        }
    }
    if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id) &&
        _topology.GetRefineLevel() > 0) {
        _topology.SetSubdivTags(sceneDelegate->GetSubdivTags(id));
    }
    if (HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, id)) {
        _topology = HdMeshTopology(_topology,
            sceneDelegate->GetDisplayStyle(id).refineLevel);
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _transform = sceneDelegate->GetTransform(id);
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        _UpdateVisibility(sceneDelegate, dirtyBits);
    }

    if (HdChangeTracker::IsCullStyleDirty(*dirtyBits, id)) {
        _cullStyle = GetCullStyle(sceneDelegate);
    }
    if (HdChangeTracker::IsDoubleSidedDirty(*dirtyBits, id)) {
        _doubleSided = IsDoubleSided(sceneDelegate);
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

    // The repr defines a set of geometry styles for drawing the mesh
    // (see hd/enums.h). We're ignoring points and wireframe for now, so
    // HdMeshGeomStyleSurf maps to subdivs and everything else maps to
    // HdMeshGeomStyleHull (coarse triangulated mesh).
    bool doRefine = (desc.geomStyle == HdMeshGeomStyleSurf);

    // If the subdivision scheme is "none", force us to not refine.
    doRefine = doRefine && (_topology.GetScheme() != PxOsdOpenSubdivTokens->none);

    // If the refine level is 0, triangulate instead of subdividing.
    doRefine = doRefine && (_topology.GetRefineLevel() > 0);

    // The repr defines whether we should compute smooth normals for this mesh:
    // per-vertex normals taken as an average of adjacent faces, and
    // interpolated smoothly across faces.
    _smoothNormals = !desc.flatShadingEnabled;

    // If the subdivision scheme is "none" or "bilinear", force us not to use
    // smooth normals.
    _smoothNormals = _smoothNormals &&
        (_topology.GetScheme() != PxOsdOpenSubdivTokens->none) &&
        (_topology.GetScheme() != PxOsdOpenSubdivTokens->bilinear);

    ////////////////////////////////////////////////////////////////////////
    // 3. Populate NSI prototype object.

    // If the topology has changed, or the value of doRefine has changed, we
    // need to create or recreate the NSI mesh object.
    // _GetInitialDirtyBits() ensures that the topology is dirty the first time
    // this function is called, so that the NSI mesh is always created.
    bool newMesh = false;
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id) ||
        doRefine != _refined) {

        // Create the new mesh node.
        newMesh = _CreateNSIMesh(renderParam, nsi);

        _refined = doRefine;
        // In both cases, the vertices will be (re-)populated below.
    }

    // If the refine level changed or the mesh was recreated, we need to pass
    // the refine level into the NSI subdiv object.
    if (newMesh || HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, id)) {
        const int refineLevel = _topology.GetRefineLevel();

        nsi.SetAttribute(_masterShapeHandle,
            NSI::CStringPArg("subdivision.scheme",
                refineLevel ? "catmull-clark" : ""));
    }

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
    if (newMesh || 
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        _SetNSIMeshAttributes(nsi, doRefine);
    }

    // Update visibility.
    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        nsi.SetAttribute(_attrsHandle, (NSI::IntegerArg("visibility", _sharedData.visible ? 1 : 0),
            NSI::IntegerArg("visibility.priority", 1)));
    }

    ////////////////////////////////////////////////////////////////////////
    // 4. Populate NSI instance objects.

    // If the mesh is instanced, create one new instance per transform.
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
        const auto range = _nsiMeshXformHandles.equal_range(id);
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
                nsi.Connect(_masterShapeHandle, "",
                    instanceXformHandle, "objects");
                nsi.Connect(_attrsHandle, "",
                    instanceXformHandle, "geometryattributes");
            }

            nsi.SetAttributeAtTime(instanceXformHandle, 0,
                NSI::DoubleMatrixArg("transformationmatrix",
                    transforms[i].GetArray()));
        }
    }
    // Otherwise, create our single instance (if necessary) and update
    // the transform (if necessary).
    else {
        // Check if we have the master transform.
        bool hasXform = (_nsiMeshXformHandles.count(id) > 0);
        if (!hasXform) {
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

    _material.Sync(
        sceneDelegate, renderParam, dirtyBits, nsi, GetId(),
        _masterShapeHandle);

    _primvars.Sync(
        sceneDelegate, renderParam, dirtyBits, nsi, GetId(),
        _masterShapeHandle, _faceVertexIndices);

    // Clean all dirty bits.
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
