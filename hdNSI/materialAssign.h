#ifndef HDNSI_MATERIALASSIGN_H
#define HDNSI_MATERIALASSIGN_H

#include <pxr/imaging/hd/sceneDelegate.h>

#include <nsi.hpp>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderParam;

/*
	This class handles material assignment on an rprim.
*/
class HdNSIMaterialAssign
{
public:
	void Sync(
		HdSceneDelegate *sceneDelegate,
		HdNSIRenderParam *renderParam,
		HdDirtyBits *dirtyBits,
		NSI::Context &nsi,
		const SdfPath &primId,
		const std::string &geoHandle);

	const SdfPath& GetMaterialId() const { return m_materialId; }

	void assignFacesets(
		const HdGeomSubsets &subset_broup,
		NSI::Context& nsi,
		const std::string& geoHandle);

private:
	/* Handle of connected material. */
	std::string m_assignedMaterialHandle;
	/* Id of the currently assigned material. */
	SdfPath m_materialId;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
