//
// Copyright 2016 Pixar
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

#include "instancer.h"

#include "primvars.h"
#include "pxr/imaging/hd/sceneDelegate.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/quaternion.h"
#include "pxr/base/tf/staticTokens.h"

PXR_NAMESPACE_OPEN_SCOPE

// Define local tokens for the names of the primvars the instancer
// consumes.
// XXX: These should be hydra tokens...
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (instanceTransform)
    (rotate)
    (scale)
    (translate)
);

HdNSIInstancer::HdNSIInstancer(HdSceneDelegate* delegate,
                                     SdfPath const& id,
                                     SdfPath const &parentId)
    : HdInstancer(delegate, id, parentId)
{
}

HdNSIInstancer::~HdNSIInstancer()
{
}

void
HdNSIInstancer::_SyncPrimvars()
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdChangeTracker &changeTracker = 
        GetDelegate()->GetRenderIndex().GetChangeTracker();
    SdfPath const& id = GetId();

    // Use the double-checked locking pattern to check if this instancer's
    // primvars are dirty.
    int dirtyBits = changeTracker.GetInstancerDirtyBits(id);
    if (HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, id)) {
        std::lock_guard<std::mutex> lock(_instanceLock);

        dirtyBits = changeTracker.GetInstancerDirtyBits(id);
        if (HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, id)) {

            // If this instancer has dirty primvars, get the list of
            // primvar names and then cache each one.

            TfTokenVector primvarNames;
            HdPrimvarDescriptorVector primvars = GetDelegate()
                ->GetPrimvarDescriptors(id, HdInterpolationInstance);

            for (HdPrimvarDescriptor const& pv: primvars) {
                if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name)) {
                    VtValue value = GetDelegate()->Get(id, pv.name);
                    if (!value.IsEmpty())
                    {
                        CachedPv cpv;
                        cpv.descriptor = pv;
                        cpv.value = value;
                        _primvarMap[pv.name] = cpv;
                    }
                }
            }

            // Mark the instancer as clean
            changeTracker.MarkInstancerClean(id);
        }
    }
}

VtMatrix4dArray
HdNSIInstancer::ComputeInstanceTransforms(SdfPath const &prototypeId)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    _SyncPrimvars();

    // The transforms for this level of instancer are computed by:
    // foreach(index : indices) {
    //     instancerTransform * translate(index) * rotate(index) *
    //     scale(index) * instanceTransform(index)
    // }
    // If any transform isn't provided, it's assumed to be the identity.

    GfMatrix4d instancerTransform =
        GetDelegate()->GetInstancerTransform(GetId());
    VtIntArray instanceIndices =
        GetDelegate()->GetInstanceIndices(GetId(), prototypeId);

    VtMatrix4dArray transforms(instanceIndices.size());
    for (size_t i = 0; i < instanceIndices.size(); ++i) {
        transforms[i] = instancerTransform;
    }

    // "translate" holds a translation vector for each index.
    if (_primvarMap.count(_tokens->translate) > 0)
    {
        const VtVec3fArray values =
            _primvarMap[_tokens->translate].value.Get<VtVec3fArray>();
        for (size_t i = 0; i < instanceIndices.size(); ++i)
        {
            if (size_t(instanceIndices[i]) < values.size())
            {
                const auto &v = values[instanceIndices[i]];
                GfMatrix4d translateMat(1);
                translateMat.SetTranslate(GfVec3d(v));
                transforms[i] = translateMat * transforms[i];
            }
        }
    }

    // "rotate" holds a quaternion in <real, i, j, k> format for each index.
    if (_primvarMap.count(_tokens->rotate) > 0)
    {
        const VtVec4fArray values =
            _primvarMap[_tokens->rotate].value.Get<VtVec4fArray>();
        for (size_t i = 0; i < instanceIndices.size(); ++i)
        {
            if (size_t(instanceIndices[i]) < values.size())
            {
                const auto &v = values[instanceIndices[i]];
                GfMatrix4d rotateMat(1);
                rotateMat.SetRotate(GfRotation(GfQuaternion(
                    v[0], GfVec3d(v[1], v[2], v[3]))));
                transforms[i] = rotateMat * transforms[i];
            }
        }
    }

    // "scale" holds an axis-aligned scale vector for each index.
    if (_primvarMap.count(_tokens->scale) > 0)
    {
        const VtVec3fArray values =
            _primvarMap[_tokens->scale].value.Get<VtVec3fArray>();
        for (size_t i = 0; i < instanceIndices.size(); ++i)
        {
            if (size_t(instanceIndices[i]) < values.size())
            {
                const auto &v = values[instanceIndices[i]];
                GfMatrix4d scaleMat(1);
                scaleMat.SetScale(GfVec3d(v));
                transforms[i] = scaleMat * transforms[i];
            }
        }
    }

    // "instanceTransform" holds a 4x4 transform matrix for each index.
    if (_primvarMap.count(_tokens->instanceTransform) > 0)
    {
        const VtMatrix4dArray values =
            _primvarMap[_tokens->instanceTransform].value
            .Get<VtMatrix4dArray>();
        for (size_t i = 0; i < instanceIndices.size(); ++i)
        {
            if (size_t(instanceIndices[i]) < values.size())
            {
                const auto &v = values[instanceIndices[i]];
                transforms[i] = v * transforms[i];
            }
        }
    }

    if (GetParentId().IsEmpty()) {
        return transforms;
    }

    HdInstancer *parentInstancer =
        GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
    if (!TF_VERIFY(parentInstancer)) {
        return transforms;
    }

    // The transforms taking nesting into account are computed by:
    // parentTransforms = parentInstancer->ComputeInstanceTransforms(GetId())
    // foreach (parentXf : parentTransforms, xf : transforms) {
    //     parentXf * xf
    // }
    VtMatrix4dArray parentTransforms =
        static_cast<HdNSIInstancer*>(parentInstancer)->
            ComputeInstanceTransforms(GetId());

    VtMatrix4dArray final(parentTransforms.size() * transforms.size());
    for (size_t i = 0; i < parentTransforms.size(); ++i) {
        for (size_t j = 0; j < transforms.size(); ++j) {
            final[i * transforms.size() + j] = transforms[j] *
                                               parentTransforms[i];
        }
    }
    return final;
}

/*
    Select some items from an array according to indices in another array.
*/
template<typename ArrayT>
ArrayT SelectArrayItems(const VtIntArray indices, const ArrayT data)
{
    ArrayT newData;
    newData.resize(indices.size());
    for (size_t i = 0; i < indices.size(); ++i)
    {
        if (size_t(indices[i]) < data.size())
        {
            newData[i] = data[indices[i]];
        }
    }
    return newData;
}

/*
    This exports all instance primvars which are not transform related, for a
    given prototype.
*/
void HdNSIInstancer::ExportInstancePrimvars(
    const SdfPath &prototypeId,
	HdNSIRenderParam *renderParam,
    const std::string &instancesHandle)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    _SyncPrimvars();

    VtIntArray indices =
        GetDelegate()->GetInstanceIndices(GetId(), prototypeId);

    for (const auto &item : _primvarMap)
    {
        /* Skip the transforms which are processed separately. */
        if (item.first == _tokens->translate ||
            item.first == _tokens->rotate ||
            item.first == _tokens->scale ||
            item.first == _tokens->instanceTransform )
        {
            continue;
        }

        VtValue v = item.second.value;
        if (v.IsEmpty())
            continue;

	    NSI::Context &nsi = renderParam->AcquireSceneForEdit();
        VtValue newv;
        if (v.IsHolding<VtArray<TfToken>>())
        {
            newv = SelectArrayItems(indices, v.Get<VtArray<TfToken>>());
        }
        else if (v.IsHolding<VtArray<std::string>>())
        {
            newv = SelectArrayItems(indices, v.Get<VtArray<std::string>>());
        }
        else if (v.IsHolding<VtArray<float>>())
        {
            newv = SelectArrayItems(indices, v.Get<VtArray<float>>());
        }
        else if (v.IsHolding<VtArray<GfVec2f>>())
        {
            newv = SelectArrayItems(indices, v.Get<VtArray<GfVec2f>>());
        }
        else if (v.IsHolding<VtArray<GfVec3f>>())
        {
            newv = SelectArrayItems(indices, v.Get<VtArray<GfVec3f>>());
        }
        else if (v.IsHolding<VtArray<int>>())
        {
            newv = SelectArrayItems(indices, v.Get<VtArray<int>>());
        }

        HdNSIPrimvars::SetAttributeFromValue(
            nsi, instancesHandle, item.second.descriptor, newv, 0);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
