#ifndef HDNSI_MATERIAL_H
#define HDNSI_MATERIAL_H

#include "pxr/imaging/hd/material.h"

#include "nsi.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <set>

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

	class VolumeCB
	{
	public:
		virtual void NewVDBNode(
			NSI::Context &nsi,
			HdNSIMaterial *material) = 0;
	};
	/* This set gets a mutex because it is used from multiple objects which
	   could do their Sync()/Finalize() in parallel. */
	struct VolumeCallbacks : std::set<VolumeCB*>
	{
		std::mutex m_mutex;
		void locked_insert(VolumeCB* cb)
		{
			std::lock_guard<std::mutex> guard{m_mutex};
			insert(cb);
		}
		void locked_erase(VolumeCB *cb)
		{
			std::lock_guard<std::mutex> guard{m_mutex};
			erase(cb);
		}
	};
	std::weak_ptr<VolumeCallbacks> GetVolumeCallbacks();

	const HdMaterialNode* GetVDBVolume() const { return m_vdbVolume.get(); }

	static const std::array<TfToken, 6>& VolumeNodeParameters();

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
	/* Copy of the vdbVolume node, if we have one. */
	std::unique_ptr<HdMaterialNode> m_vdbVolume;
	/* List of the callbacks to invoke when the material changes. */
	std::shared_ptr<VolumeCallbacks> m_volume_callbacks;
	/* Mutex to initialize m_volume_callbacks. */
	std::mutex m_volume_callbacks_mutex;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
