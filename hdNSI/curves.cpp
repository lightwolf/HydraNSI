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

#include "curves.h"

#include "renderDelegate.h"
#include "renderParam.h"
#include "renderPass.h"

#include <pxr/imaging/hd/basisCurves.h>

#include <sstream>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

HdNSICurves::HdNSICurves(
    SdfPath const& id
    DECLARE_IID)
    : HdBasisCurves(id PASS_IID)
    , HdNSIRprimBase{"curves"}
{
}

void
HdNSICurves::Finalize(HdRenderParam *renderParam)
{
    HdNSIRprimBase::Finalize(static_cast<HdNSIRenderParam*>(renderParam));
}

HdDirtyBits
HdNSICurves::GetInitialDirtyBitsMask() const
{
    // The initial dirty bits control what data is available on the first
    // run through _PopulateRtCurves(), so it should list every data item
    // that _PopulateRtCurves requests.
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::InitRepr
        | HdChangeTracker::DirtyPoints
        | HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyCullStyle
        | HdChangeTracker::DirtyDoubleSided
        | HdChangeTracker::DirtyDisplayStyle
        | HdChangeTracker::DirtySubdivTags
        | HdChangeTracker::DirtyWidths
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyNormals
        | HdChangeTracker::DirtyInstancer
        | HdChangeTracker::DirtyInstanceIndex
        | HdChangeTracker::DirtyMaterialId
        | HdNSIRprimBase::ProcessedDirtyBits()
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

    /* The base rprim class tracks this but does not update it itself. */
    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, GetId()))
    {
        _UpdateVisibility(sceneDelegate, dirtyBits);
    }
#if PXR_VERSION > 2011
	_UpdateInstancer(sceneDelegate, dirtyBits);
#endif

    /* This creates the NSI nodes so it comes before other attributes. */
    HdNSIRprimBase::Sync(sceneDelegate, nsiRenderParam, dirtyBits, *this);

    /* Update curve specific attributes. */
    _PopulateRtCurves(sceneDelegate, nsiRenderParam, nsi, dirtyBits, desc);
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

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id))
    {
        const HdBasisCurvesTopology &topology =
            GetBasisCurvesTopology(sceneDelegate);

        const VtIntArray &vertexCounts = topology.GetCurveVertexCounts();
        nsi.SetAttribute(Shape(),
            *NSI::Argument("nvertices")
            .SetType(NSITypeInteger)
            ->SetCount(vertexCounts.size())
            ->SetValuePointer(vertexCounts.cdata()));

        TfToken basis = topology.GetCurveBasis();

        /* Default for unsupported basis, such as bezier. */
        const char *basisName = "catmull-rom";
        if (topology.GetCurveType() == HdTokens->linear)
            basisName = "linear";
        else if (basis == HdTokens->catmullRom)
            basisName = "catmull-rom";
        else if (basis == HdTokens->bSpline)
            basisName = "b-spline";

        nsi.SetAttribute(Shape(), (
            NSI::StringArg("basis", basisName),
            NSI::IntegerArg("extrapolate", 1)));
    }

    _material.Sync(
        sceneDelegate, renderParam, dirtyBits, nsi, GetId(), Shape());

    _primvars.Sync(
        sceneDelegate, renderParam, dirtyBits, nsi, GetId(),
        Shape(), VtIntArray()); // _curveVertexIndices ?

    // Clean all dirty bits.
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
