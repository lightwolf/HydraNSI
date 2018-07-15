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

#include "pxr/imaging/hdNSI/config.h"
#include "pxr/imaging/hdNSI/instancer.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/imaging/hdNSI/renderPass.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/matrix4d.h"

#include <sstream>
#include <iostream>

#include <nsi.h>
#include <nsi.hpp>

PXR_NAMESPACE_OPEN_SCOPE

std::multimap<std::string, std::string> HdNSIMesh::_nsiMeshAttrShaderHandles; // static

std::map<SdfPath, std::string> HdNSIMesh::_nsiMeshShapeHandles; // static

std::multimap<SdfPath, std::string> HdNSIMesh::_nsiMeshXformHandles; // static

HdNSIMesh::HdNSIMesh(SdfPath const& id,
                     SdfPath const& instancerId)
    : HdMesh(id, instancerId)
    , _color(0.3f, 0.3f, 0.3f, 1.0f)
    , _refined(false)
    , _smoothNormals(false)
    , _doubleSided(false)
    , _cullStyle(HdCullStyleDontCare)
{
}

void
HdNSIMesh::Finalize(HdRenderParam *renderParam)
{
    NSIContext_t ctx = static_cast<HdNSIRenderParam*>(renderParam)
        ->AcquireSceneForEdit();
    NSI::Context nsi(ctx);

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

    // Delete the shader and attributes.
    char colorKey[256];
    sprintf(colorKey, "%.3f %.3f %.3f", _color[0], _color[1], _color[2]);

    if (_nsiMeshAttrShaderHandles.count(colorKey)) {
        auto range = _nsiMeshAttrShaderHandles.equal_range(colorKey);
        for (auto itr = range.first; itr != range.second; ++ itr) {
            const std::string &handle = itr->second;
            nsi.Delete(handle);
        }
        _nsiMeshAttrShaderHandles.erase(colorKey);
    }

    _shaderHandle.clear();
    _attrsHandle.clear();

    // //
    // _nsiMeshGeoAttrHandle.clear();
    // XXX TODO: remove the shader
    // nsi.Delete(_nsiDefaultShaderHandle);
    // nsi.Delete(_nsiMeshGeoAttrHandle);
}

HdDirtyBits
HdNSIMesh::_GetInitialDirtyBits() const
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
        | HdChangeTracker::DirtyRefineLevel
        | HdChangeTracker::DirtySubdivTags
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyNormals
        | HdChangeTracker::DirtyInstanceIndex
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
HdNSIMesh::_UpdateRepr(HdSceneDelegate *sceneDelegate,
                          TfToken const &reprName,
                          HdDirtyBits *dirtyBits)
{
    TF_UNUSED(sceneDelegate);
    TF_UNUSED(reprName);
    TF_UNUSED(dirtyBits);
    // NSI doesn't use the HdRepr structure.
}

void
HdNSIMesh::Sync(HdSceneDelegate* sceneDelegate,
                   HdRenderParam*   renderParam,
                   HdDirtyBits*     dirtyBits,
                   TfToken const&   reprName,
                   bool             forcedRepr)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // The repr token is used to look up an HdMeshReprDesc struct, which
    // has drawing settings for this prim to use. Repr opinions can come
    // from the render pass's rprim collection or the scene delegate;
    // _GetReprName resolves these multiple opinions.
    TfToken calculatedReprName = _GetReprName(reprName, forcedRepr);

    // XXX: Meshes can have multiple reprs; this is done, for example, when
    // the drawstyle specifies different rasterizing modes between front faces
    // and back faces. With raytracing, this concept makes less sense, but
    // combining semantics of two HdMeshReprDesc is tricky in the general case.
    // For now, HdNSIMesh only respects the first desc; this should be fixed.
    _MeshReprConfig::DescArray descs = _GetReprDesc(calculatedReprName);
    const HdMeshReprDesc &desc = descs[0];

    // Pull top-level NSI state out of the render param.
    HdNSIRenderParam *NSIRenderParam =
        static_cast<HdNSIRenderParam*>(renderParam);
    NSIContext_t ctx = NSIRenderParam->AcquireSceneForEdit();
    // NSIContext_t device = NSIRenderParam->GetNSIContext();

    // Create NSI geometry objects.
    _PopulateRtMesh(sceneDelegate, ctx, dirtyBits, desc);
}

bool
HdNSIMesh::_CreateNSIMesh(NSIContext_t ctx)
{
    NSI::Context nsi(ctx);

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
    const HdNSIConfig &config = HdNSIConfig::GetInstance();
    if (config.meshClockwisewinding) {
        nsi.SetAttribute(_masterShapeHandle, NSI::IntegerArg("clockwisewinding", 1));
    }

    // Create the master transform node.
    const std::string &masterXformHandle = id.GetString() + "|transform1";

    nsi.Create(masterXformHandle, "transform");
    nsi.Connect(masterXformHandle, "", NSI_SCENE_ROOT, "objects");
    nsi.Connect(_masterShapeHandle, "", masterXformHandle, "objects");

    _nsiMeshXformHandles.insert(std::make_pair(id, masterXformHandle));

    // Create the shader.
    // XXX TODO: probably shader attribute setting should be moved to
    // _SetNSIMeshAttributes() to handle changes in color/...

    // Create the shader based on the color.
    char colorKey[256];
    sprintf(colorKey, "%.3f %.3f %.3f", _color[0], _color[1], _color[2]);

    if (_nsiMeshAttrShaderHandles.count(colorKey)) {
        // Find the shader and attribute node.
        auto range = _nsiMeshAttrShaderHandles.equal_range(colorKey);
        for (auto itr = range.first; itr != range.second; ++ itr) {
            const std::string &handle = itr->second;
            if (handle.find("|default") != std::string::npos) {
                _shaderHandle = handle;
            } else if (handle.find("|attribute") != std::string::npos) {
                _attrsHandle = handle;
            }
        }
    } else {
        // Create the default shader node.
        _shaderHandle = std::string(colorKey) + "|default1";

        const std::string &shaderPath =
            config.delight + "/maya/osl/dl3DelightMaterial";

        nsi.Create(_shaderHandle, "shader");
        nsi.SetAttribute(_shaderHandle,
            (NSI::StringArg("shaderfilename", shaderPath),
                NSI::ColorArg("i_color", _color.data())));

        _nsiMeshAttrShaderHandles.insert(
            std::make_pair(colorKey, _shaderHandle));

        // Create the attribute node.
        _attrsHandle = std::string(colorKey) + "|attributes1";

        nsi.Create(_attrsHandle, "attributes");
        nsi.Connect(_shaderHandle, "", _attrsHandle, "surfaceshader");

        _nsiMeshAttrShaderHandles.insert(
            std::make_pair(colorKey, _attrsHandle));
    }

    nsi.Connect(_attrsHandle, "", masterXformHandle, "geometryattributes");

    return newShape;
}

void
HdNSIMesh::_SetNSIMeshAttributes(NSIContext_t ctx, bool asSubdiv)
{
    NSI::Context nsi(ctx);

    NSI::ArgumentList attrs;

    // Set if this mesh is subdivision.
    if (asSubdiv) {
        nsi.SetAttribute(_masterShapeHandle,
            NSI::CStringPArg("subdivision.scheme", "catmull-clark"));
    }

    // Subdivision-related attributes.
    if (asSubdiv) {
        const PxOsdSubdivTags &subdivTags = _topology.GetSubdivTags();

        const VtIntArray &creaseIndices = subdivTags.GetCreaseIndices();
        const VtFloatArray &creaseWeights = subdivTags.GetCreaseWeights();
        if (creaseIndices.size() && creaseWeights.size()) {
        }
    }

    // "nvertices"
    attrs.push(NSI::Argument::New("nvertices")
        ->SetType(NSITypeInteger)
        ->SetCount(_faceVertexCounts.size())
        ->SetValuePointer(_faceVertexCounts.data()));

    // "P"
    attrs.push(NSI::Argument::New("P")
        ->SetType(NSITypePoint)
        ->SetCount(_points.size())
        ->SetValuePointer(_points.cdata()));

    // "P.indices"
    attrs.push(NSI::Argument::New("P.indices")
        ->SetType(NSITypeInteger)
        ->SetCount(_faceVertexIndices.size())
        ->SetValuePointer(_faceVertexIndices.data()));

    // "N"
    if (_normals.size()) {
        attrs.push(NSI::Argument::New("N")
            ->SetType(NSITypeNormal)
            ->SetCount(_normals.size())
            ->SetValuePointer(_normals.cdata()));

        attrs.push(NSI::Argument::New("N.indices")
            ->SetType(NSITypeInteger)
            ->SetCount(_faceVertexIndices.size())
            ->SetValuePointer(_faceVertexIndices.data()));
    }

    nsi.SetAttribute(_masterShapeHandle, attrs);
}

void
HdNSIMesh::_UpdatePrimvarSources(HdSceneDelegate* sceneDelegate,
                                    HdDirtyBits dirtyBits)
{
    HD_TRACE_FUNCTION();
    SdfPath const& id = GetId();

    // Update _primvarSourceMap, our local cache of raw primvar data.
    // This function pulls data from the scene delegate, but defers processing.
    //
    // While iterating primvars, we skip "points" (vertex positions) because
    // the points primvar is processed by _PopulateRtMesh. We only call
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
HdNSIMesh::_PopulateRtMesh(HdSceneDelegate* sceneDelegate,
                              NSIContext_t           ctx,
                              HdDirtyBits*     dirtyBits,
                              HdMeshReprDesc const &desc)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();

    NSI::Context nsi(ctx);

    ////////////////////////////////////////////////////////////////////////
    // 1. Pull scene data.

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        VtValue pointsValue = sceneDelegate->Get(id, HdTokens->points);
        _points = pointsValue.Get<VtVec3fArray>();

        // Get the color of the object if possible.
        _color.Set(0.3f, 0.3f, 0.3f, 1.0f);

        VtValue colorValue = sceneDelegate->Get(id, HdTokens->color);
        if (!colorValue.IsEmpty()) {
            VtVec4fArray colors = colorValue.Get<VtVec4fArray>();
            if (colors.size()) {
                _color = colors[0];
            }
        }
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

        _adjacency.BuildAdjacencyTable(&_topology);
        _normals = _adjacency.ComputeSmoothNormals(_points.size(), _points.data());
    }
    if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id)) {
        _topology.SetSubdivTags(sceneDelegate->GetSubdivTags(id));
    }
    if (HdChangeTracker::IsRefineLevelDirty(*dirtyBits, id)) {
        _topology = HdMeshTopology(_topology,
            sceneDelegate->GetRefineLevel(id));
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
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals) ||
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths) ||
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->primvar)) {
        _UpdatePrimvarSources(sceneDelegate, *dirtyBits);
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
    _smoothNormals = desc.smoothNormals;

    // If the subdivision scheme is "none" or "bilinear", force us not to use
    // smooth normals.
    _smoothNormals = _smoothNormals &&
        (_topology.GetScheme() != PxOsdOpenSubdivTokens->none) &&
        (_topology.GetScheme() != PxOsdOpenSubdivTokens->bilinear);

    // If the scene delegate has provided authored normals, force us to not use
    // smooth normals.
    bool authoredNormals = false;
    if (_primvarSourceMap.count(HdTokens->normals) > 0) {
        authoredNormals = true;
    }
    _smoothNormals = _smoothNormals && !authoredNormals;

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
        newMesh = _CreateNSIMesh(ctx);

        _refined = doRefine;
        // In both cases, the vertices will be (re-)populated below.
    }

    // If the refine level changed or the mesh was recreated, we need to pass
    // the refine level into the NSI subdiv object.
    if (newMesh || HdChangeTracker::IsRefineLevelDirty(*dirtyBits, id)) {
        const int refineLevel = _topology.GetRefineLevel();

        nsi.SetAttribute(_masterShapeHandle,
            NSI::CStringPArg("subdivision.scheme",
                refineLevel ? "catmull-clark" : ""));
    }

    // Populate/update points in the NSI mesh.
    if (newMesh || 
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        _SetNSIMeshAttributes(ctx, doRefine);
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

    // Clean all dirty bits.
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
