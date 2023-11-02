#ifndef HDNSI_VOLUME_H
#define HDNSI_VOLUME_H

#include "compatibility.h"
#include "material.h"
#include "materialAssign.h"
#include "rprimBase.h"

#include <pxr/imaging/hd/volume.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

/**
	\brief Volume primitive.
*/
class HdNSIVolume final :
	public HdVolume, protected HdNSIMaterial::VolumeCB, public HdNSIRprimBase
{
public:
	HdNSIVolume(
		const SdfPath &id
		DECLARE_IID);

	virtual ~HdNSIVolume() override = default;

	HdNSIVolume(const HdNSIVolume&) = delete;
	void operator=(const HdNSIVolume&) = delete;

	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

	virtual void Sync(
		HdSceneDelegate *sceneDelegate,
		HdRenderParam *renderParam,
		HdDirtyBits *dirtyBits,
		const TfToken &reprName) override;

	virtual void Finalize(HdRenderParam *renderParam) override;

protected:
	virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

	virtual void _InitRepr(
		const TfToken &reprName,
		HdDirtyBits *dirtyBits) override;

	bool HasField(const TfToken &name) const;

	virtual void NewVDBNode(
		NSI::Context &nsi,
		HdNSIMaterial *material) override;

private:
	HdNSIMaterialAssign m_material;
	/* Assigned material. */
	SdfPath m_materialId;
	/* Assigned material's callback list. */
	std::weak_ptr<HdNSIMaterial::VolumeCallbacks> m_volume_callbacks;
	/* Valid fields. */
	HdVolumeFieldDescriptorVector m_fields;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
