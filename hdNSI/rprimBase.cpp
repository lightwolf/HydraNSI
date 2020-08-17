#include "rprimBase.h"

#include "pointInstancer.h"
#include "renderDelegate.h"

#include <pxr/imaging/hd/rprim.h>

#include <numeric>

PXR_NAMESPACE_OPEN_SCOPE

void HdNSIRprimBase::Sync(
	HdSceneDelegate *sceneDelegate,
	HdNSIRenderParam *renderParam,
	HdDirtyBits *dirtyBits,
	const HdRprim &rprim)
{
	NSI::Context &nsi = renderParam->AcquireSceneForEdit();
	bool first = _masterShapeHandle.empty();

	/* Make sure the nodes are created. */
	Create(nsi, rprim);

	SdfPath const& id = rprim.GetId();

	/* Update instancer. */
	/* FIXME: Track invalidation properly instead of always updating. */
	if (!rprim.GetInstancerId().IsEmpty())
	{
		HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
		auto instancer = static_cast<HdNSIPointInstancer*>(
			renderIndex.GetInstancer(rprim.GetInstancerId()));
		instancer->SyncPrototype(renderParam, id, _xformHandle, first);
	}

	/* The transform of the rprim itself. */
	if (HdChangeTracker::IsTransformDirty(*dirtyBits, id))
	{
		ExportTransform(sceneDelegate, id, false, nsi, _xformHandle);
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
		nsi.SetAttribute(_attrsHandle,
			NSI::IntegerArg("visibility", rprim.IsVisible() ? 1 : 0));
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

	nsi.Delete(_attrsHandle);
	_attrsHandle.clear();
}

/**
	\brief Sample and export the transform for a prim.

	\param sceneDelegate
		The scene delegate. Duh.
	\param id
		The prim's id.
	\param isInstancer
		Because Hydra APIs are dumb and there is a different call to get the
		transform of an instancer.
	\param nsi
		The NSI context.
	\param handle
		The transform node handle to export to.
*/
void HdNSIRprimBase::ExportTransform(
	HdSceneDelegate *sceneDelegate,
	const SdfPath &id,
	bool isInstancer,
	NSI::Context &nsi,
	const std::string &handle)
{
	HdTimeSampleArray<GfMatrix4d, 4> samples;
	if (isInstancer)
	{
		sceneDelegate->SampleInstancerTransform(id, &samples);
	}
	else
	{
		sceneDelegate->SampleTransform(id, &samples);
	}
	if( samples.count == 1 )
	{
		nsi.SetAttribute(handle,
			NSI::DoubleMatrixArg("transformationmatrix",
				samples.values[0].GetArray()));
	}
	else
	{
		/* Delete previous motion samples so we don't add to them. */
		nsi.DeleteAttribute(handle, "transformationmatrix");
		/* Output the new samples. */
		for (size_t i = 0; i < samples.count; ++i )
		{
			nsi.SetAttributeAtTime(handle, samples.times[i],
				NSI::DoubleMatrixArg("transformationmatrix",
					samples.values[i].GetArray()));
		}
	}
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
		/* The instancer will connect the prototype to itself. */
	}

	/* Create the attributes node. */
	_attrsHandle = id.GetString() + "|attr";
	nsi.Create(_attrsHandle, "attributes");
	nsi.Connect(_attrsHandle, "", _masterShapeHandle, "geometryattributes");
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
