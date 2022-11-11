#ifndef HDNSI_CAMERADATA_H
#define HDNSI_CAMERADATA_H

#include <pxr/base/gf/range1d.h>
#include <pxr/base/gf/range2d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/timeSampleArray.h>
#include <pxr/usd/sdf/path.h>

#include <nsi.hpp>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderParam;

class HdNSICameraData
{
public:
	HdNSICameraData(
		const SdfPath &id);

	void UpdateExportedCamera(
		const HdNSICameraData &new_data,
		HdNSIRenderParam *renderParam,
		NSI::Context &nsi);

	void Delete(
		NSI::Context &nsi);

	bool IsNew() const { return m_new; }
	void SetUsed() const { m_new = false; }

	std::string GetCameraNode() const { return m_camera_handle; }
	std::string GetTransformNode() const { return m_xform_handle; }

	GfRange2d GetAperture() const;

	const GfMatrix4d& GetProjectionMatrix() const
		{ return m_projection_matrix; }


	void SetViewMatrix(const GfMatrix4d &view);
	HdTimeSampleArray<GfMatrix4d, 4>* TransformSamples()
		{ return &m_transform; }

	void SetProjectionMatrix(const GfMatrix4d &proj);

	void SetDoF(
		double focallength,
		double fstop,
		double focaldistance);
	void DisableDoF() { m_dof_enable = false; }

	void SetShutterRange(const GfRange1d &r) { m_shutter_range = r; }

private:
	bool IsPerspective() const;
	void Create(
		HdNSIRenderParam *renderParam,
		NSI::Context &nsi);

private:
	SdfPath m_id;

	/* NSI handles of created nodes. */
	std::string m_camera_handle;
	std::string m_xform_handle;

	/*
		Flag to indicate the camera node is newly created. This is used to
		avoid an ABA type problem if a camera gets deleted and recreated with
		the same Id. The render pass needs to know it is new to reconnect other
		nodes to it.
	*/
	mutable bool m_new{true};

	/* True if the created camera node is perspective type. */
	bool m_is_perspective{false};

	/* Projection matrix. */
	GfMatrix4d m_projection_matrix;

	/* Aperture. */
	GfVec2d m_aperture_min{-1.0}, m_aperture_max{1.0};

	/* Transform. */
	HdTimeSampleArray<GfMatrix4d, 4> m_transform;

	/* Clipping range. */
	GfRange1d m_clipping_range;

	/* Field of view, for perspective camera. */
	float m_fov{90.0f};

	/* Depth of field. */
	bool m_dof_enable{false};
	double m_dof_focallength{0.0};
	double m_dof_fstop{0.0};
	double m_dof_focaldistance{0.0};

	/* Shutter range. */
	GfRange1d m_shutter_range;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
