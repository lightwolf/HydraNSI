#include "materialAssign.h"

#include "renderDelegate.h"
#include "renderParam.h"

PXR_NAMESPACE_OPEN_SCOPE

void HdNSIMaterialAssign::Sync(
	HdSceneDelegate *sceneDelegate,
	HdNSIRenderParam *renderParam,
	HdDirtyBits *dirtyBits,
	NSI::Context &nsi,
	const SdfPath &primId,
	const std::string &geoHandle)
{
	if (0 == (*dirtyBits & HdChangeTracker::DirtyMaterialId))
		return;

	/* Remove previous material, if any. */
	if (!m_assignedMaterialHandle.empty())
	{
		nsi.Disconnect(
			m_assignedMaterialHandle, "",
			geoHandle, "geometryattributes");
		m_assignedMaterialHandle.clear();
	}
	/* Figure out the new material to use. */
	m_materialId = sceneDelegate->GetMaterialId(primId);
	std::string mat = m_materialId.GetString();
	if (mat.empty())
	{
		/* Use the default material. */
		m_assignedMaterialHandle =
			renderParam->GetRenderDelegate()->DefaultMaterialHandle();
	}
	else
	{
		m_assignedMaterialHandle = mat + "|mat";
	}
	/* Connect it. */
	nsi.Connect(
		m_assignedMaterialHandle, "",
		geoHandle, "geometryattributes");

	*dirtyBits &= ~HdDirtyBits(HdChangeTracker::DirtyMaterialId);
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
