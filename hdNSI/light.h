#ifndef HDNSI_LIGHT_H
#define HDNSI_LIGHT_H

#include <pxr/imaging/hd/light.h>

#include <nsi.hpp>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderParam;

class HdNSILight : public HdLight
{
public:
	HdNSILight(
		const TfToken &typeId,
		const SdfPath &sprimId);

	virtual void Sync(
		HdSceneDelegate *sceneDelegate,
		HdRenderParam *renderParam,
		HdDirtyBits *dirtyBits) override;

	virtual void Finalize(HdRenderParam *renderParam) override;

	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

private:
	void CreateNodes(
		HdNSIRenderParam *renderParam,
		NSI::Context &i_nsi);

	void DeleteNodes(
		HdNSIRenderParam *renderParam,
		NSI::Context &i_nsi);

	void SetShaderParams(
		NSI::Context &i_nsi,
		HdSceneDelegate *sceneDelegate);

	void SyncVisibilityAndLinking(
		NSI::Context &i_nsi,
		HdSceneDelegate *sceneDelegate);

	void GenCylinder(
		NSI::Context &i_nsi,
		const std::string &i_geo,
		float i_length,
		float i_radius);

private:
	const TfToken m_typeId;
	bool m_nodes_created;
	/* Handle of the attributes node used for light linking, if created. */
	std::string m_linking_attr_handle;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
