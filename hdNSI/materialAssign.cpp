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

void HdNSIMaterialAssign::assignFacesets(
	const HdGeomSubsets &subset_group,
	NSI::Context& nsi,
	const std::string& geoHandle)
{
	if (subset_group.empty())
	{
		return;
	}

	for (size_t i = 0; i < subset_group.size(); i++)
	{
		const HdGeomSubset &subset = subset_group[i];

		/*
			If a goemetry contains subsets and a material is connected to it,
			all subsets will have that material connected too. We don't need
			to connect the same material to the subsets of the geometry.
		*/
		if (subset.materialId.IsEmpty() || subset.materialId == m_materialId)
			continue;

		std::string subset_mat = subset.materialId.GetString() + "|mat";
		std::string subset_id = subset.id.GetString();

		/*
			TODO: We should track the faceset nodes we create so the old ones
			can be deleted on updates, and eventually in a Finalize() method
			when the prim is removed. Not a big deal for now as Houdini always
			recreates the whole primitive when the subsets changed.
		*/
		nsi.Create(subset_id, "faceset");

		//Necessary to interactively update the shaders connected.
		//FIXME: Find a better solution rather than disconnecting all.
		nsi.Disconnect(".all", "",
			subset_id, "geometryattributes");

		nsi.Connect(
			subset_mat, "",
			subset_id, "geometryattributes");
		nsi.Connect(
			subset_id, "",
			geoHandle, "facesets");

		VtIntArray indices = subset.indices;
		if (!indices.empty())
		{
			nsi.SetAttribute(subset_id,
				*NSI::Argument("faces")
				.SetType(NSITypeInteger)
				->SetCount(indices.size())
				->SetValuePointer(indices.data()));
		}
	}
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
