#include "camera.h"

#include "renderParam.h"

#include <pxr/imaging/hd/sceneDelegate.h>

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

HdNSICamera::HdNSICamera(
	const SdfPath &id)
:
	HdCamera{id}
{
}

void HdNSICamera::Sync(
	HdSceneDelegate *sceneDelegate,
	HdRenderParam *renderParam,
	HdDirtyBits *dirtyBits)
{
	auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
	NSI::Context &nsi = nsiRenderParam->AcquireSceneForEdit();

	/* Cache this because HdCamera clears all of them. */
	auto bits = *dirtyBits;
	/* Let HdCamera retrieve its data. */
	HdCamera::Sync(sceneDelegate, renderParam, dirtyBits);
	assert(*dirtyBits == Clean);

	/* Create the nodes now that we know which kind of projection is used. */
	Create(nsi);

	NSI::ArgumentList args;

	if (bits & DirtyProjMatrix)
	{
		const GfMatrix4d &proj = GetProjectionMatrix();
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

		/* Add to camera attributes. */
		double clipping_range[2] = {clip_near, clip_far};
		args.push(NSI::Argument::New("clippingrange")
			->SetType(NSITypeDouble)
			->SetCount(2)
			->CopyValue(clipping_range, sizeof(clipping_range)));

		if( IsPerspective() )
		{
			/* Compute FoV from the matrix. */
			double fov = 2.0 * std::atan(1.0 / proj[1][1]);
			fov = GfRadiansToDegrees(fov);
			args.push(new NSI::FloatArg("fov", fov));

			/* Adjust aperture accordingly (NSI FoV is for vertical [-1, 1]). */
			m_aperture_min *= proj[1][1];
			m_aperture_max *= proj[1][1];
		}
	}

	if (bits & DirtyViewMatrix)
	{
		nsi.SetAttribute(m_xform_handle,
			NSI::DoubleMatrixArg("transformationmatrix",
				GetViewInverseMatrix().data()));
	}

	if (bits & DirtyWindowPolicy)
	{
		/* This is handled in HdNSIRenderPass, which manages the screen. */
	}

	if (bits & DirtyParams)
	{
		/* DoF */
		VtValue v_focallength = sceneDelegate->GetCameraParamValue(
			GetId(), HdCameraTokens->focalLength);
		VtValue v_fstop = sceneDelegate->GetCameraParamValue(
			GetId(), HdCameraTokens->fStop);
		VtValue v_focusdistance = sceneDelegate->GetCameraParamValue(
			GetId(), HdCameraTokens->focusDistance);

		if (v_focallength.IsHolding<float>() &&
		    v_fstop.IsHolding<float>() && v_fstop.Get<float>() > 0.0f &&
		    v_focusdistance.IsHolding<float>())
		{
			args.push(new NSI::DoubleArg("depthoffield.focallength",
				v_focallength.Get<float>()));
			args.push(new NSI::DoubleArg("depthoffield.fstop",
				v_fstop.Get<float>()));
			args.push(new NSI::DoubleArg("depthoffield.focaldistance",
				v_focusdistance.Get<float>()));
			args.push(new NSI::IntegerArg("depthoffield.enable", 1));
		}
		else
		{
			args.push(new NSI::IntegerArg("depthoffield.enable", 0));
		}

		/* Shutter for motion blur. */
		VtValue v_shutteropen = sceneDelegate->GetCameraParamValue(
			GetId(), HdCameraTokens->shutterOpen);
		VtValue v_shutterclose = sceneDelegate->GetCameraParamValue(
			GetId(), HdCameraTokens->shutterClose);
		if (v_shutteropen.IsHolding<double>() &&
		    v_shutterclose.IsHolding<double>())
		{
			double sr[2] =
				{ v_shutteropen.Get<double>(), v_shutterclose.Get<double>() };
			args.push(NSI::Argument::New("shutterrange")
				->SetType(NSITypeDouble)
				->SetCount(2)
				->CopyValue(sr, sizeof(sr)));
		}
		else
		{
			nsi.DeleteAttribute(m_camera_handle, "shutterrange");
		}
	}

	nsi.SetAttribute(m_camera_handle, args);
}

HdDirtyBits HdNSICamera::GetInitialDirtyBitsMask() const
{
	return
		DirtyParams |
		HdCamera::GetInitialDirtyBitsMask();
}

void HdNSICamera::Finalize(HdRenderParam *renderParam)
{
	auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
	NSI::Context &nsi = nsiRenderParam->AcquireSceneForEdit();

	/*
		Stop rendering in case the camera being deleted is the one being
		rendered. Removal of cameras should be a rare enough event to not make
		this a usability issue. If not, we'll have to check if it's actually
		the one being rendered.
	*/
	nsiRenderParam->StopRender();

	nsi.Delete(m_camera_handle);
	m_camera_handle.clear();

	nsi.Delete(m_xform_handle);
	m_xform_handle.clear();

	HdCamera::Finalize(renderParam);
}

GfRange2d HdNSICamera::GetAperture() const
{
	return { m_aperture_min, m_aperture_max };
}

bool HdNSICamera::IsPerspective() const
{
	return GetProjectionMatrix()[3][3] == 0.0;
}

void HdNSICamera::Create(NSI::Context &nsi)
{
	if (!m_camera_handle.empty())
		return;

	const SdfPath &id = GetId();
	m_camera_handle = id.GetString() + "|camera";
	m_xform_handle = id.GetString();

	nsi.Create(m_camera_handle, IsPerspective()
		? "perspectivecamera" : "orthographiccamera");
	nsi.Create(m_xform_handle, "transform");
	nsi.Connect(m_camera_handle, "", m_xform_handle, "objects");
	nsi.Connect(m_xform_handle, "", NSI_SCENE_ROOT, "objects");
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
