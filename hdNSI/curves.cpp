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

#include "pxr/imaging/hdNSI/config.h"
#include "pxr/imaging/hdNSI/instancer.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/imaging/hdNSI/renderPass.h"
#include "pxr/imaging/hd/basisCurves.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/matrix4d.h"

#include <sstream>
#include <iostream>

#include <nsi.h>
#include <nsi.hpp>

PXR_NAMESPACE_OPEN_SCOPE

std::multimap<size_t, std::string> HdNSICurves::_nsiCurvesAttrShaderHandles; // static
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
    NSIContext_t ctx = static_cast<HdNSIRenderParam*>(renderParam)
        ->AcquireSceneForEdit();
    NSI::Context nsi(ctx);

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

    // Delete shader and attributes node.
    const size_t colorsKey =
        boost::hash_range(_colors.begin(), _colors.end());

    if (_nsiCurvesAttrShaderHandles.count(colorsKey)) {
        auto range = _nsiCurvesAttrShaderHandles.equal_range(colorsKey);
        for (auto itr = range.first; itr != range.second; ++ itr) {
            const std::string &handle = itr->second;
            nsi.Delete(handle);
        }
    }

    _nsiCurvesAttrShaderHandles.erase(colorsKey);

    // Delete the geometry.
    if (_nsiCurvesShapeHandles.count(id)) {
        _nsiCurvesShapeHandles.erase(id);
        nsi.Delete(_masterShapeHandle);
    }

    _masterShapeHandle.clear();
}

HdDirtyBits
HdNSICurves::_GetInitialDirtyBits() const
{
    // The initial dirty bits control what data is available on the first
    // run through _PopulateRtCurves(), so it should list every data item
    // that _PopulateRtCurves requests.
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
HdNSICurves::_UpdateRepr(HdSceneDelegate *sceneDelegate,
                          TfToken const &reprName,
                          HdDirtyBits *dirtyBits)
{
    TF_UNUSED(sceneDelegate);
    TF_UNUSED(reprName);
    TF_UNUSED(dirtyBits);
    // NSI doesn't use the HdRepr structure.
}

void
HdNSICurves::Sync(HdSceneDelegate* sceneDelegate,
                   HdRenderParam*   renderParam,
                   HdDirtyBits*     dirtyBits,
                   TfToken const&   reprName,
                   bool             forcedRepr)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // The repr token is used to look up an HdBasisCurvesReprDesc struct, which
    // has drawing settings for this prim to use. Repr opinions can come
    // from the render pass's rprim collection or the scene delegate;
    // _GetReprName resolves these multiple opinions.
    TfToken calculatedReprName = _GetReprName(reprName, forcedRepr);

    // XXX: Curveses can have multiple reprs; this is done, for example, when
    // the drawstyle specifies different rasterizing modes between front faces
    // and back faces. With raytracing, this concept makes less sense, but
    // combining semantics of two HdBasisCurvesReprDesc is tricky in the general case.
    // For now, HdNSICurves only respects the first desc; this should be fixed.
    _BasisCurvesReprConfig::DescArray descs = _GetReprDesc(calculatedReprName);
    const HdBasisCurvesReprDesc &desc = descs[0];

    // Pull top-level NSI state out of the render param.
    HdNSIRenderParam *NSIRenderParam =
        static_cast<HdNSIRenderParam*>(renderParam);
    NSIContext_t ctx = NSIRenderParam->AcquireSceneForEdit();
    // NSIContext_t device = NSIRenderParam->GetNSIContext();

    // Create NSI geometry objects.
    _PopulateRtCurves(sceneDelegate, ctx, dirtyBits, desc);
}

void
HdNSICurves::_CreateNSICurves(NSIContext_t ctx)
{
    NSI::Context nsi(ctx);

    const SdfPath &id = GetId();
    _masterShapeHandle = id.GetString() + "|curves1";

    // Create the new curves.
    nsi.Create(_masterShapeHandle, "cubiccurves");

    // Create the master transform node.
    const std::string &masterXformHandle = id.GetString() + "|transform1";

    nsi.Create(masterXformHandle, "transform");
    nsi.Connect(masterXformHandle, "", NSI_SCENE_ROOT, "objects");
    nsi.Connect(_masterShapeHandle, "", masterXformHandle, "objects");

    _nsiCurvesXformHandles.insert(std::make_pair(id, masterXformHandle));

    // Create the hair shader based on the hashed color array.
    _shaderHandle = id.GetString() + "|shader1";
    _attrsHandle = id.GetString() + "|attributes1";

    const size_t colorsKey = boost::hash_range(_colors.begin(), _colors.end());

    if (!_nsiCurvesAttrShaderHandles.count(colorsKey)) {
        // Create the dlHairAndFur shader.
        const HdNSIConfig &config = HdNSIConfig::GetInstance();
        std::string shaderPath = config.delight + "/maya/osl/dlHairAndFur";

        nsi.Create(_shaderHandle, "shader");
        nsi.SetAttribute(_shaderHandle, NSI::StringArg("shaderfilename", shaderPath));
        nsi.SetAttribute(_shaderHandle, NSI::FloatArg("dye_weight", 0.8f));

        _nsiCurvesAttrShaderHandles.insert(std::make_pair(colorsKey, _shaderHandle));

        // Create the dlPrimitiveAttribute shader.
        shaderPath = config.delight + "/maya/osl/dlPrimitiveAttribute";
        std::string displayColorShaderHandle = id.GetString() + "|shader2";
        nsi.Create(displayColorShaderHandle, "shader");
        nsi.SetAttribute(displayColorShaderHandle, (NSI::StringArg("shaderfilename", shaderPath),
            NSI::StringArg("attribute_name", "DisplayColor"),
            NSI::IntegerArg("attribute_type", 1)));
        nsi.Connect(displayColorShaderHandle, "o_color", _shaderHandle, "i_color");

        _nsiCurvesAttrShaderHandles.insert(std::make_pair(colorsKey, displayColorShaderHandle));

        // Create the attribute node and connect shader to this.
        nsi.Create(_attrsHandle, "attributes");
        nsi.Connect(_shaderHandle, "", _attrsHandle, "surfaceshader");

        _nsiCurvesAttrShaderHandles.insert(std::make_pair(colorsKey, _attrsHandle));
    }

    nsi.Connect(_attrsHandle, "", masterXformHandle, "geometryattributes");
}

void
HdNSICurves::_SetNSICurvesAttributes(NSIContext_t ctx)
{
    NSI::Context nsi(ctx);

    NSI::ArgumentList attrs;

    // "nvertices"
    attrs.push(NSI::Argument::New("nvertices")
        ->SetType(NSITypeInteger)
        ->SetCount(_curveVertexCounts.size())
        ->SetValuePointer(_curveVertexCounts.data()));

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

    // "DisplayColor" for the shader.
    attrs.push(NSI::Argument::New("DisplayColor")
        ->SetType(NSITypeColor)
        ->SetCount(_colors.size())
        ->SetValuePointer(_colors.data()));

    nsi.SetAttribute(_masterShapeHandle, attrs);
}

void
HdNSICurves::_UpdatePrimvarSources(HdSceneDelegate* sceneDelegate,
                                    HdDirtyBits dirtyBits)
{
    HD_TRACE_FUNCTION();
    SdfPath const& id = GetId();

    // Update _primvarSourceMap, our local cache of raw primvar data.
    // This function pulls data from the scene delegate, but defers processing.
    //
    // While iterating primvars, we skip "points" (vertex positions) because
    // the points primvar is processed by _PopulateRtCurves. We only call
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
HdNSICurves::_PopulateRtCurves(HdSceneDelegate* sceneDelegate,
                              NSIContext_t           ctx,
                              HdDirtyBits*     dirtyBits,
                              HdBasisCurvesReprDesc const &desc)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();

    bool newCurves = false;

    ////////////////////////////////////////////////////////////////////////
    // 1. Pull scene data.

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals) ||
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths) ||
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->primvar)) {
        _UpdatePrimvarSources(sceneDelegate, *dirtyBits);
    }

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

        // Get the colors.
        _colors.clear();

        if (_primvarSourceMap.count(HdTokens->color)) {
            const VtValue &_colorsVal = _primvarSourceMap[HdTokens->color].data;
            const VtVec4fArray &colors = _colorsVal.Get<VtVec4fArray>();

            _colors.resize(colors.size());
            for (size_t i = 0; i < colors.size(); ++ i) {
                const GfVec4f &color = colors[i];
                _colors[i] = GfVec3f(color[0], color[1], color[2]);
            }
        }

        if (_colors.empty()) {
            _colors.resize(_points.size());
            std::fill(_colors.begin(), _colors.end(), GfVec3f(0.5f, 0, 0.5f));
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

    int refineLevel = GetRefineLevel(sceneDelegate);
    bool doRefine = (refineLevel > 0);

    ////////////////////////////////////////////////////////////////////////
    // 3. Populate NSI prototype object.

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id) ||
        doRefine != _refined) {

        newCurves = true;

        // Create the new curves node.
        _CreateNSICurves(ctx);

        _refined = doRefine;
        // In both cases, vertices/attributes will be (re-)populated below.
    }

    // Populate/update points in the NSI curves.
    if (newCurves || 
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        _SetNSICurvesAttributes(ctx);
    }

    ////////////////////////////////////////////////////////////////////////
    // 4. Populate NSI instance objects.

    // If the curves is instanced, create one new instance per transform.
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
