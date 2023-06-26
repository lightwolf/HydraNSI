#include "rprimBase.h"

#include "pointInstancer.h"
#include "renderDelegate.h"

#include <pxr/imaging/hd/rprim.h>

#include <cmath>
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

	/* Update instancer's data. */
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

	/*
		Update categories. We only do light linking with those for now so we
		make some assumptions in here. If we ever need to tell what's what,
		lights always get synchronized first so we could store a global list.
	*/
	if (0 != (*dirtyBits & HdChangeTracker::DirtyCategories))
	{
		VtArray<TfToken> categories = sceneDelegate->GetCategories(id);
		/* Reconnecting everything from scratch is the easiest way to update. */
		nsi.Disconnect(_attrsHandle, "", NSI_ALL_NODES, "visibility");
		for( const TfToken &cat : categories )
		{
			nsi.Connect(_attrsHandle, "", cat.GetString(), "visibility",
				NSI::IntegerArg("value", 1));
		}
	}

	/* Clear the bits for what we processed. */
	*dirtyBits &= ~ProcessedDirtyBits();
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
	ExportTransform(samples, nsi, handle);
}

/**
	\brief Export the transform for a prim.

	\param samples
		The transform data.
	\param nsi
		The NSI context.
	\param handle
		The transform node handle to export to.
*/
void HdNSIRprimBase::ExportTransform(
	const HdTimeSampleArray<GfMatrix4d, 4> &samples,
	NSI::Context &nsi,
	const std::string &handle)
{
	/* Check for invalid time values. Houdini sends NaN on an empty scene. */
	size_t count = samples.count;
	for (size_t i = 0; i < count; ++i )
	{
		if( !std::isfinite(samples.times[i]) )
			count = 1;
	}
	if( count == 1 )
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
		for (size_t i = 0; i < count; ++i )
		{
			nsi.SetAttributeAtTime(handle, samples.times[i],
				NSI::DoubleMatrixArg("transformationmatrix",
					samples.values[i].GetArray()));
		}
	}
}

/**
	\brief Equality comparison according to how we export transforms.

	Much like an operator== except that it considers non finite time values to
	be equivalent. Which they are in our export as we don't export them.
*/
bool HdNSIRprimBase::SameTransform(
	const HdTimeSampleArray<GfMatrix4d, 4> &a,
	const HdTimeSampleArray<GfMatrix4d, 4> &b)
{
	if( a.count != b.count )
		return false;
	for( size_t i = 0; i < a.count; ++i )
	{
		if( (std::isfinite(a.times[i]) || std::isfinite(b.times[i])) &&
		    a.times[i] != b.times[i] )
			return false;
		if( a.values[i] != b.values[i] )
			return false;
	}
	return true;
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
