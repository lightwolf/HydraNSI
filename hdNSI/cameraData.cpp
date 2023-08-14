#include "cameraData.h"

#include "renderDelegate.h"
#include "renderParam.h"
#include "rprimBase.h"
#include "tokens.h"

#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

HdNSICameraData::HdNSICameraData(
	const SdfPath &id)
:
	m_base{id.IsEmpty() ? ":defaultcamera:" : id.GetString()}
{
}

/**
	\brief Update this data and issue NSI commands for the change.

	\returns
		true if any of the data used by the screen export has changed.

	The camera data from new_data, except id and handles, is assigned to this
	object. Then NSI calls are issues to update the scene according to the
	changes.
*/
bool HdNSICameraData::UpdateExportedCamera(
	const HdNSICameraData &new_data,
	HdNSIRenderParam *renderParam)
{
	/* Check for changes which require a screen update. */
	bool has_change =
		m_aperture_min != new_data.m_aperture_min ||
		m_aperture_max != new_data.m_aperture_max;

	/* Copy the data which does not get exported directly here. The projection
	   matrix affects the camera type in Create() however. */
	m_projection_matrix = new_data.m_projection_matrix;
	m_aperture_min = new_data.m_aperture_min;
	m_aperture_max = new_data.m_aperture_max;

	/* Create the nodes now that we know which kind of projection is used. */
	Create(renderParam);

	NSI::ArgumentList args;

	if( !HdNSIRprimBase::SameTransform(m_transform, new_data.m_transform) )
	{
		m_transform = new_data.m_transform;
		NSI::Context &nsi = renderParam->AcquireSceneForEdit();
		HdNSIRprimBase::ExportTransform(
			m_transform, nsi, m_xform_handle);
	}

	if( m_clipping_range != new_data.m_clipping_range )
	{
		m_clipping_range = new_data.m_clipping_range;
		double cr[2] = {m_clipping_range.GetMin(), m_clipping_range.GetMax()};
		args.push(NSI::Argument::New("clippingrange")
			->SetType(NSITypeDouble)
			->SetCount(2)
			->CopyValue(cr, sizeof(cr)));
	}

	if( m_fov != new_data.m_fov )
	{
		m_fov = new_data.m_fov;
		args.push(new NSI::FloatArg("fov", m_fov));
	}

	/*
		If necessary, && the global DoF enable setting with new_data's. It is
		important this does not get done from HdNSICamera::Sync() or the
		camera's DoF enable state will be lost.
	*/
	bool new_dof_enable = new_data.m_dof_enable;
	if( m_use_global_settings && new_dof_enable )
	{
		new_dof_enable = renderParam->GetRenderDelegate()->
			GetRenderSetting<bool>(HdNSIRenderSettingsTokens->enableDoF, true);
	}

	if( m_dof_enable != new_dof_enable ||
	    m_dof_focallength != new_data.m_dof_focallength ||
	    m_dof_fstop != new_data.m_dof_fstop ||
	    m_dof_focaldistance != new_data.m_dof_focaldistance )
	{
		m_dof_enable = new_dof_enable;
		m_dof_focallength = new_data.m_dof_focallength;
		m_dof_fstop = new_data.m_dof_fstop;
		m_dof_focaldistance = new_data.m_dof_focaldistance;

		args.push(new NSI::IntegerArg("depthoffield.enable", m_dof_enable));
		if( m_dof_enable )
		{
			args.push(new NSI::DoubleArg("depthoffield.focallength",
				m_dof_focallength));
			args.push(new NSI::DoubleArg("depthoffield.fstop",
				m_dof_fstop));
			args.push(new NSI::DoubleArg("depthoffield.focaldistance",
				m_dof_focaldistance));
		}
	}

	if( m_shutter_range != new_data.m_shutter_range )
	{
		m_shutter_range = new_data.m_shutter_range;
		if( m_shutter_range.IsEmpty() )
		{
			NSI::Context &nsi = renderParam->AcquireSceneForEdit();
			nsi.DeleteAttribute(m_camera_handle, "shutterrange");
		}
		else
		{
			double sr[2] =
				{ m_shutter_range.GetMin(), m_shutter_range.GetMax() };
			args.push(NSI::Argument::New("shutterrange")
				->SetType(NSITypeDouble)
				->SetCount(2)
				->CopyValue(sr, sizeof(sr)));
		}
	}

	if( !args.empty() )
	{
		NSI::Context &nsi = renderParam->AcquireSceneForEdit();
		nsi.SetAttribute(m_camera_handle, args);
	}

	return has_change;
}

/**
	\brief Delete the NSI nodes created for this object.
*/
void HdNSICameraData::Delete(
	NSI::Context &nsi)
{
	nsi.Delete(m_camera_handle);
	m_camera_handle.clear();

	nsi.Delete(m_xform_handle);
	m_xform_handle.clear();
}

GfRange2d HdNSICameraData::GetAperture() const
{
	return { m_aperture_min, m_aperture_max };
}

void HdNSICameraData::SetViewMatrix(const GfMatrix4d &view)
{
	m_transform.Resize(1);
	m_transform.times[0] = 0.0;
	m_transform.values[0] = view;
}

void HdNSICameraData::SetProjectionMatrix(const GfMatrix4d &proj)
{
	m_projection_matrix = proj;
	const GfMatrix4d invProj = proj.GetInverse();

	/* Extract aperture. */
	double z1 = proj.Transform(GfVec3d(0, 0, -1))[2];
	m_aperture_min = GfVec2d(invProj.Transform(GfVec3d(-1, -1, z1)).data());
	m_aperture_max = GfVec2d(invProj.Transform(GfVec3d(1, 1, z1)).data());

	/* Extract clipping range. */
	double clip_near = -
		(proj[3][2] - -1.0 * proj[3][3]) /
		(-1.0 * proj[2][3] - proj[2][2]);
	double clip_far = -
		(proj[3][2] -  1.0 * proj[3][3]) /
		( 1.0 * proj[2][3] - proj[2][2]);

	m_clipping_range = GfRange1d{clip_near, clip_far};

	if( IsPerspective() )
	{
		/* Compute FoV from the matrix. */
		double fov = 2.0 * std::atan(1.0 / proj[1][1]);
		m_fov = GfRadiansToDegrees(fov);

		/* Adjust aperture accordingly (NSI FoV is for vertical [-1, 1]). */
		m_aperture_min *= proj[1][1];
		m_aperture_max *= proj[1][1];
	}
}

void HdNSICameraData::SetDoF(
	double focallength,
	double fstop,
	double focaldistance)
{
	m_dof_enable = true;
	m_dof_focallength = focallength;
	m_dof_fstop = fstop;
	m_dof_focaldistance = focaldistance;
}

bool HdNSICameraData::IsPerspective() const
{
	return GetProjectionMatrix()[3][3] == 0.0;
}

void HdNSICameraData::Create(HdNSIRenderParam *renderParam)
{
	bool is_perspective = IsPerspective();

	if (!m_camera_handle.empty() && is_perspective == m_is_perspective)
		return;

	NSI::Context &nsi = renderParam->AcquireSceneForEdit();
	if (!m_camera_handle.empty())
	{
		/*
			Camera type change requires replacing the node. This amounts to a
			camera change, which requires stopping the render.

			We don't check if this camera is the one actually being rendered
			because the only case I've seen of this so far is usdview's camera
			which sometimes gets initialized with an identity matrix before
			being given its correct projection. It is somewhat random.
		*/
		renderParam->StopRender();
		nsi.Delete(m_camera_handle);
	}

	/* Needed for the type change case. */
	m_new = true;

	m_camera_handle = m_base + "|camera";
	m_xform_handle = m_base;

	m_is_perspective = is_perspective;
	nsi.Create(m_camera_handle, is_perspective
		? "perspectivecamera" : "orthographiccamera");
	nsi.Create(m_xform_handle, "transform");
	nsi.Connect(m_camera_handle, "", m_xform_handle, "objects");
	nsi.Connect(m_xform_handle, "", NSI_SCENE_ROOT, "objects");
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
