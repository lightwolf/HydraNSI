#include "field.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/sdf/assetPath.h>

PXR_NAMESPACE_OPEN_SCOPE

HdNSIField::HdNSIField(const SdfPath &id)
:
	HdField{id}
{
}

void HdNSIField::Sync(
	HdSceneDelegate *sceneDelegate,
	HdRenderParam *renderParam,
	HdDirtyBits *dirtyBits)
{
	/* This is unused here. Fetched directly in HdNSIVolume for now. */
#if 0
	VtValue path_v = sceneDelegate->Get(GetId(), HdFieldTokens->filePath);
	if (path_v.IsHolding<SdfAssetPath>())
	{
		std::string path = path_v.Get<SdfAssetPath>().GetResolvedPath();
	}
#endif

	*dirtyBits = Clean;
}

HdDirtyBits HdNSIField::GetInitialDirtyBitsMask() const
{
	return AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
