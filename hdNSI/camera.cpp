#include "camera.h"

#include "renderDelegate.h"
#include "renderParam.h"
#include "rprimBase.h"

#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/sceneDelegate.h>

PXR_NAMESPACE_OPEN_SCOPE

HdNSICamera::HdNSICamera(
	const SdfPath &id)
:
	HdCamera{id},
	m_exported_data{id}
{
}

void HdNSICamera::Sync(
	HdSceneDelegate *sceneDelegate,
	HdRenderParam *renderParam,
	HdDirtyBits *dirtyBits)
{
	auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);

	/* Cache this because HdCamera clears all of them. */
	auto bits = *dirtyBits;
	/* Let HdCamera retrieve its data. */
	HdCamera::Sync(sceneDelegate, renderParam, dirtyBits);
	assert(*dirtyBits == Clean);

	/* Make a copy of camera data, that we'll do the updates into. */
	HdNSICameraData data = m_exported_data;

#if defined(PXR_VERSION) && PXR_VERSION <= 2111
	if (bits & DirtyProjMatrix)
#else
	/* The matrix is computed from params exclusively, in HdCamera. */
	if (bits & DirtyParams)
#endif
	{
		SyncProjectionMatrix(data);
	}

#if defined(PXR_VERSION) && PXR_VERSION <= 2111
	if (bits & DirtyViewMatrix)
#else
	if (bits & DirtyTransform)
#endif
	{
		sceneDelegate->SampleTransform(GetId(), data.TransformSamples());
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
			data.SetDoF(
				v_focallength.Get<float>(),
				v_fstop.Get<float>(),
				v_focusdistance.Get<float>());
		}
		else
		{
			data.DisableDoF();
		}

		/* Shutter for motion blur. */
		VtValue v_shutteropen = sceneDelegate->GetCameraParamValue(
			GetId(), HdCameraTokens->shutterOpen);
		VtValue v_shutterclose = sceneDelegate->GetCameraParamValue(
			GetId(), HdCameraTokens->shutterClose);
		if (v_shutteropen.IsHolding<double>() &&
		    v_shutterclose.IsHolding<double>())
		{
			data.SetShutterRange(
				{v_shutteropen.Get<double>(), v_shutterclose.Get<double>()});
		}
		else
		{
			/*
				Look for a default shutter setting. This is a bit of a hack to
				get motion blur in the houdini viewport until it gives us a
				proper camera.
			*/
			static TfToken shutter_token{
				"nsi:global:defaultshutter", TfToken::Immortal};
			VtValue default_shutter = nsiRenderParam->GetRenderDelegate()
				->GetRenderSetting(shutter_token);
			if (default_shutter.IsHolding<GfVec2d>())
			{
				auto r = default_shutter.Get<GfVec2d>();
				data.SetShutterRange(GfRange1d{r[0], r[1]});
			}
			else
			{
				/* You will have no motion blur today. */
				data.SetShutterRange(GfRange1d{});
			}
		}
	}

	/* Do the necessary NSI calls for what was updated. */
	m_exported_data.UpdateExportedCamera(data, nsiRenderParam);
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

	m_exported_data.Delete(nsi);

	HdCamera::Finalize(renderParam);
}

#if defined(PXR_VERSION) && PXR_VERSION <= 2111
void HdNSICamera::SyncFromState(
	const HdRenderPassState &renderPassState,
	HdNSIRenderParam *nsiRenderParam)
{
	GfMatrix4d view = renderPassState.GetWorldToViewMatrix();
	GfMatrix4d proj = renderPassState.GetProjectionMatrix();
	/* Fix garbage projection. */
	if (proj[2][2] > 0.0)
	{
		proj[2][2] *= -1.0;
	}
	if (view == GetViewMatrix() && proj == GetProjectionMatrix())
	{
		/* Don't issue updates if nothing has changed. */
		return;
	}

	_worldToViewMatrix = view;
	_worldToViewInverseMatrix = _worldToViewMatrix.GetInverse();
	_projectionMatrix = proj;

	/* Make a copy of camera data, that we'll do the updates into. */
	HdNSICameraData data = m_exported_data;

	SyncProjectionMatrix(data);
	data.SetViewMatrix(view);

	/* Do the necessary NSI calls for what was updated. */
	m_exported_data.UpdateExportedCamera(data, nsiRenderParam);
}
#endif

void HdNSICamera::SyncProjectionMatrix(
	HdNSICameraData &sync_data)
{
#if defined(PXR_VERSION) && PXR_VERSION <= 2111
	const GfMatrix4d &proj = GetProjectionMatrix();
#else
	const GfMatrix4d &proj = ComputeProjectionMatrix();
#endif
	sync_data.SetProjectionMatrix(proj);
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
