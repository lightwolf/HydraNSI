#ifndef HDNSI_CAMERA_H
#define HDNSI_CAMERA_H

#include <pxr/base/gf/range2d.h>
#include <pxr/imaging/hd/camera.h>

#include <nsi.hpp>

PXR_NAMESPACE_OPEN_SCOPE

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

	std::string GetCameraNode() const { return m_camera_handle; }
	std::string GetTransformNode() const { return m_xform_handle; }

	GfRange2d GetAperture() const;

private:
	bool IsPerspective() const;
	void Create(NSI::Context &nsi);

	/* NSI handles. */
	std::string m_camera_handle;
	std::string m_xform_handle;;

	GfVec2d m_aperture_min{-1.0}, m_aperture_max{1.0};
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
