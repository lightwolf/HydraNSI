#ifndef HDNSI_CAMERA_H
#define HDNSI_CAMERA_H

#include "cameraData.h"

#include <pxr/imaging/hd/camera.h>

#include <nsi.hpp>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderParam;
class HdRenderPassState;

class HdNSICamera final : public HdCamera
{
public:
	HdNSICamera(
		const SdfPath &id);

	virtual ~HdNSICamera() override = default;

	HdNSICamera(const HdNSICamera&) = delete;
	void operator=(const HdNSICamera&) = delete;

	virtual void Sync(
		HdSceneDelegate *sceneDelegate,
		HdRenderParam *renderParam,
		HdDirtyBits *dirtyBits) override;

	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

	virtual void Finalize(HdRenderParam *renderParam) override;

#if defined(PXR_VERSION) && PXR_VERSION <= 2111
	void SyncFromState(
		const HdRenderPassState &renderPassState,
		HdNSIRenderParam *nsiRenderParam);
#endif

	const HdNSICameraData& Data() const { return m_exported_data; }

private:
	void SyncProjectionMatrix(
		HdNSICameraData &sync_data);

	HdNSICameraData m_exported_data;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
