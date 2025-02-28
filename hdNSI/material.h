#ifndef HDNSI_MATERIAL_H
#define HDNSI_MATERIAL_H

#include <pxr/imaging/hd/material.h>

#include <nsi.hpp>

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

#if defined(PXR_VERSION) && PXR_VERSION <= 2008
	virtual void Reload() override;
#endif

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
	struct DefaultConnectionList;

	void UseDefaultShader(
		NSI::Context &nsi,
		HdNSIRenderParam *renderParam,
		const std::string &mat_handle,
		bool use_default);

	void ExportNetworks(
		NSI::Context &nsi,
		HdNSIRenderParam *renderParam,
		const HdMaterialNetworkMap &networks);

	void ExportNode(
		NSI::Context &nsi,
		HdNSIRenderParam *renderParam,
		const HdMaterialNode &node,
		DefaultConnectionList &default_connections);

	void DeleteShaderNodes(NSI::Context &nsi);
	void DeleteOneNetwork(
		NSI::Context &nsi,
		HdMaterialNetwork &network,
		const HdMaterialNetwork &new_network);

	static std::string EscapeOSLKeyword(const std::string &name);
	static std::string DecodeArrayIndex(const std::string &name);

private:
	/* true once the attributes node has been created. */
	bool m_attributes_created;
	/* true when we've connected the default shader. */
	bool m_use_default_shader{false};
	/* Currently exported materials for the terminals we support. */
	HdMaterialNetwork m_surface_network;
	HdMaterialNetwork m_displacement_network;
	HdMaterialNetwork m_volume_network;
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
