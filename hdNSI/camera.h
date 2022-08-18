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

#if defined(PXR_VERSION) && PXR_VERSION <= 2111
	void SyncFromState(
		const HdRenderPassState &renderPassState,
		HdNSIRenderParam *nsiRenderParam);
#endif

	bool IsNew() const { return m_new; }
	void SetUsed() const { m_new = false; }

	std::string GetCameraNode() const { return m_camera_handle; }
	std::string GetTransformNode() const { return m_xform_handle; }

	GfRange2d GetAperture() const;

	/* '2' is because older versions have a HdCamera::GetProjectionMatrix() */
	const GfMatrix4d& GetProjectionMatrix2() const
		{ return m_projection_matrix; }

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

	/*
		Flag to indicate the camera has never been rendered. This is used to
		avoid an ABA type problem if a camera gets deleted and recreated with
		the same Id. The render pass needs to know it is new to reconnect other
		nodes to it.
	*/
	mutable bool m_new{true};
	/* True if the created camera node is perspective type. */
	bool m_is_perspective{0};

	GfVec2d m_aperture_min{-1.0}, m_aperture_max{1.0};

	/* Last computed projection matrix. */
	GfMatrix4d m_projection_matrix;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
