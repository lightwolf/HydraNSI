#ifndef HDNSI_MATERIAL_H
#define HDNSI_MATERIAL_H

#include "pxr/imaging/hd/material.h"

#include "nsi.hpp"

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderParam;

class HdNSIMaterial : public HdMaterial
{
public:
	HdNSIMaterial(
		const SdfPath &sprimId);

	virtual void Reload() override;

	virtual void Sync(
		HdSceneDelegate *sceneDelegate,
		HdRenderParam *renderParam,
		HdDirtyBits *dirtyBits) override;

	virtual void Finalize(HdRenderParam *renderParam) override;

	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

private:
	void ExportNetworks(
		NSI::Context &nsi,
		HdNSIRenderParam *renderParam,
		const HdMaterialNetworkMap &networks);

	void ExportNode(
		NSI::Context &nsi,
		HdNSIRenderParam *renderParam,
		const HdMaterialNode &node);

	void DeleteShaderNodes(NSI::Context &nsi);

	static std::string EscapeOSLKeyword(const std::string &name);

private:
	/* true once the attributes node has been created. */
	bool m_attributes_created;
	/* Handles to all the nodes of the material's network. */
	std::vector<std::string> m_network_nodes;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
