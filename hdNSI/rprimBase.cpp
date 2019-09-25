#include "pxr/imaging/hdNSI/rprimBase.h"

#include "pxr/imaging/hd/rprim.h"
#include "pxr/imaging/hdNSI/instancer.h"
#include "pxr/imaging/hdNSI/renderDelegate.h"

#include <numeric>

PXR_NAMESPACE_OPEN_SCOPE

void HdNSIRprimBase::Sync(
	HdSceneDelegate *sceneDelegate,
	HdNSIRenderParam *renderParam,
	HdDirtyBits *dirtyBits,
	const HdRprim &rprim)
{
	NSI::Context &nsi = renderParam->AcquireSceneForEdit();

	/* Make sure the nodes are created. */
	Create(nsi, rprim);

	CheckPrimvars(sceneDelegate, dirtyBits, rprim);

	SdfPath const& id = rprim.GetId();

	/* Update instance transforms. */
	/* FIXME: Track invalidation properly instead of always updating. */
	if (!rprim.GetInstancerId().IsEmpty())
	{
		/* Retrieve instance transforms from the instancer. */
		HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
		HdNSIInstancer *instancer = static_cast<HdNSIInstancer*>(
			renderIndex.GetInstancer(rprim.GetInstancerId()));
		const VtMatrix4dArray &transforms = instancer->
			ComputeInstanceTransforms(id);

		/* Hope GfMatrix4d* and double* are equivalent :) */
		nsi.SetAttribute(_instancesHandle,
			*NSI::Argument("transformationmatrices")
			.SetType(NSITypeDoubleMatrix)
			->SetCount(transforms.size())
			->SetValuePointer(transforms.data()));

        /* Add the instanceId attribute for that AOV. */
        std::vector<int> instanceid(transforms.size());
        std::iota(instanceid.begin(), instanceid.end(), 0);
        nsi.SetAttribute(_instancesHandle,
            *NSI::Argument("instanceId")
            .SetType(NSITypeInteger)
            ->SetCount(instanceid.size())
            ->SetValuePointer(instanceid.data()));

		/* Export generic instance primvars. */
		instancer->ExportInstancePrimvars(id, renderParam, _instancesHandle);
	}

	/* The transform of the rprim itself. */
	if (HdChangeTracker::IsTransformDirty(*dirtyBits, id))
	{
		GfMatrix4d transform = sceneDelegate->GetTransform(id);
		nsi.SetAttribute(_xformHandle,
			NSI::DoubleMatrixArg("transformationmatrix", transform.GetArray()));
	}

	/* Output the primId. */
	if (HdChangeTracker::IsPrimIdDirty(*dirtyBits, id))
	{
		nsi.SetAttribute(_masterShapeHandle,
			NSI::IntegerArg("primId", rprim.GetPrimId()));
	}

	/* Update visibility. */
	if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id))
	{
		nsi.SetAttribute(_attrsHandle, (
			NSI::IntegerArg("visibility", rprim.IsVisible() ? 1 : 0),
			NSI::IntegerArg("visibility.priority", 1)));
	}

	/* Clear the bits for what we processed. */
	*dirtyBits &= ~HdDirtyBits(HdChangeTracker::Clean
		| HdChangeTracker::DirtyPrimID
		| HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
		);
}

void HdNSIRprimBase::Finalize(HdNSIRenderParam *renderParam)
{
	NSI::Context &nsi = renderParam->AcquireSceneForEdit();

	nsi.Delete(_masterShapeHandle);
	_masterShapeHandle.clear();

	nsi.Delete(_xformHandle);
	_xformHandle.clear();

	nsi.Delete(_instancesHandle);
	_instancesHandle.clear();

	nsi.Delete(_attrsHandle);
	_attrsHandle.clear();
}

void HdNSIRprimBase::Create(
	NSI::Context &nsi,
	const HdRprim &rprim)
{
	if (!_masterShapeHandle.empty())
		return;

	const SdfPath &id = rprim.GetId();
	_masterShapeHandle = id.GetString() + "|geo";

	nsi.Create(_masterShapeHandle, _nodeType);

	_xformHandle = id.GetString();
	nsi.Create(_xformHandle, "transform");
	nsi.Connect(_masterShapeHandle, "", _xformHandle, "objects");
	if (rprim.GetInstancerId().IsEmpty())
	{
		/* Just the one instance. */
		nsi.Connect(_xformHandle, "", NSI_SCENE_ROOT, "objects");
	}
	else
	{
		/* Go through an instances node to have multiple instances. */
		_instancesHandle = id.GetString() + "|instances";
		nsi.Create(_instancesHandle, "instances");
		nsi.Connect(_instancesHandle, "", NSI_SCENE_ROOT, "objects");
		nsi.Connect(_xformHandle, "", _instancesHandle, "sourcemodels");
	}

	/* Create the attributes node. */
	_attrsHandle = id.GetString() + "|attr";
	nsi.Create(_attrsHandle, "attributes");
	nsi.Connect(_attrsHandle, "", _masterShapeHandle, "geometryattributes");
}

/*
	This is a workaround for Hydra not having a nice way of letting us know if
	some specific primvars exist without spitting out a lot of warnings.

	What we do here is enumerate primvars and remove some dirty flags if the
	primvar does not exist.
*/
void HdNSIRprimBase::CheckPrimvars(
	HdSceneDelegate *sceneDelegate,
	HdDirtyBits *dirtyBits,
	const HdRprim &rprim)
{
	if (!_firstSync)
		return;

	_firstSync = false;

	bool haveNormals = false, haveWidths = false;
	for (int i = 0; i < HdInterpolationCount; ++i)
	{
		auto descriptors = rprim.GetPrimvarDescriptors(
			sceneDelegate, (HdInterpolation)i);
		for (const auto &d : descriptors)
		{
			if (d.name == HdTokens->normals)
				haveNormals = true;
			else if (d.name == HdTokens->widths)
				haveWidths = true;
		}
	}

	if (!haveNormals)
		*dirtyBits &= ~HdDirtyBits(HdChangeTracker::DirtyNormals);
	if (!haveWidths)
		*dirtyBits &= ~HdDirtyBits(HdChangeTracker::DirtyWidths);
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
