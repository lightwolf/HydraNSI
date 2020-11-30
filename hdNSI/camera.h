#ifndef HDNSI_CAMERA_H
#define HDNSI_CAMERA_H

#include <pxr/base/gf/range2d.h>
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

	void SyncFromState(
		const HdRenderPassState &renderPassState,
		HdNSIRenderParam *nsiRenderParam);

	std::string GetCameraNode() const { return m_camera_handle; }
	std::string GetTransformNode() const { return m_xform_handle; }

	GfRange2d GetAperture() const;

private:
	bool IsPerspective() const;
	void Create(
		HdNSIRenderParam *renderParam,
		NSI::Context &nsi);
	void SyncProjectionMatrix(
		NSI::ArgumentList &args);

	/* NSI handles. */
	std::string m_camera_handle;
	std::string m_xform_handle;

	/* Camera node iteration, for type changes. */
	int m_camera_gen{0};
	/* True if the created camera node is perspective type. */
	bool m_is_perspective{0};

	GfVec2d m_aperture_min{-1.0}, m_aperture_max{1.0};
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
