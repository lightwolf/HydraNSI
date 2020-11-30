//
// Copyright 2016 Pixar
// Copyright 2018 Illumination Research Pte Ltd.
// Authors: J Cube Inc (Marco Pantaleoni, Bo Zhou, Paolo Berto Durante)
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#include "renderPass.h"

#include "camera.h"
#include "mesh.h"
#include "renderDelegate.h"
#include "renderParam.h"
#include "tokens.h"

#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/usd/usdRender/tokens.h>

#include <atomic>

PXR_NAMESPACE_OPEN_SCOPE

HdNSIRenderPass::HdNSIRenderPass(
	HdRenderIndex *index,
	HdRprimCollection const &collection,
	HdNSIRenderDelegate *renderDelegate,
	HdNSIRenderParam *renderParam)
	: HdRenderPass(index, collection)
#if defined(PXR_VERSION) && PXR_VERSION <= 2002
	, _colorBuffer{SdfPath::EmptyPath()}
	, _depthBuffer{SdfPath::EmptyPath()}
#endif
	, _width(0)
	, _height(0)
	, _renderDelegate(renderDelegate)
	, _renderParam(renderParam)
{
	static std::atomic<unsigned> pass_counter{0};
	_handlesPrefix = "pass" + std::to_string(++pass_counter);
}

HdNSIRenderPass::~HdNSIRenderPass()
{
	/* Delete the placeholder cam if one was used. */
	if (m_placeholder_camera)
	{
		m_placeholder_camera->Finalize(_renderParam);
		m_placeholder_camera.reset();
	}

	// Stop the render.
	NSI::Context &nsi = _renderParam->AcquireSceneForEdit();

	_renderParam->StopRender();
	nsi.RenderControl(NSI::CStringPArg("action", "wait"));
}

bool HdNSIRenderPass::IsConverged() const
{
	/*
		Propagate converged flag to all the render buffers. It's a little weird
		to do this here but it works.
	*/
	for( const auto &b : _aovBindings )
	{
		static_cast<HdNSIRenderBuffer*>(b.renderBuffer)->SetConverged(
			_renderParam->IsConverged());
	}
	return _renderParam->IsConverged();
}

void HdNSIRenderPass::RenderSettingChanged(const TfToken &key)
{
	if (key == HdNSIRenderSettingsTokens->pixelSamples)
	{
		SetOversampling();
	}
	if (key == HdNSIRenderSettingsTokens->cameraLightIntensity)
	{
		if (!m_headlight_xform.empty())
			ExportNSIHeadLightShader();
	}
}

void HdNSIRenderPass::_Execute(
	HdRenderPassStateSharedPtr const& renderPassState,
	TfTokenVector const &renderTags)
{
	GfVec4f vp = renderPassState->GetViewport();
	auto *camera = static_cast<const HdNSICamera*>(
		renderPassState->GetCamera());
	if (!camera)
	{
		/* Use the placeholder camera object. */
		if (!m_placeholder_camera)
		{
			m_placeholder_camera.reset(new HdNSICamera({}));
		}
		m_placeholder_camera->SyncFromState(*renderPassState, _renderParam);
		camera = m_placeholder_camera.get();
	}
	/* If either the viewport or the selected camera changes, update screen. */
	if (_width != vp[2] || _height != vp[3] ||
	   m_render_camera != camera->GetCameraNode())
	{
		_width = vp[2];
		_height = vp[3];
		/* Resolution/camera changes required stopping the render. */
		_renderParam->StopRender();
		UpdateScreen(*renderPassState, camera);
	}

	// If the list of AOVs changed, update the outputs.
	HdRenderPassAovBindingVector aovBindings =
		renderPassState->GetAovBindings();

	if( _outputNodes.empty() || aovBindings != _aovBindings )
	{
		_aovBindings = aovBindings;
#if defined(PXR_VERSION) && PXR_VERSION <= 2002
		if( aovBindings.empty() )
		{
			_colorBuffer.Allocate(
				GfVec3i(_width, _height, 1), HdFormatUNorm8Vec4, true);
			_depthBuffer.Allocate(
				GfVec3i(_width, _height, 1), HdFormatFloat32, true);
			HdRenderPassAovBinding aov;
			aov.aovName = HdAovTokens->color;
			aov.renderBuffer = &_colorBuffer;
			aovBindings.push_back(aov);
			aov.aovName = HdAovTokens->depth;
			aov.renderBuffer = &_depthBuffer;
			aovBindings.push_back(aov);
		}
#endif
		/* Output changes required stopping the render. */
		_renderParam->StopRender();
		UpdateOutputs(aovBindings);
	}

	/* The output driver needs part of the projection matrix to remap Z. */
	const GfMatrix4d &projMatrix = camera->GetProjectionMatrix();
	_depthProj.M22 = projMatrix[2][2];
	_depthProj.M32 = projMatrix[3][2];

	/* Enable headlight if there are no lights in the scene. */
	UpdateHeadlight(!_renderParam->HasLights(), camera);

	if (!_renderParam->IsRendering())
	{
		/* Start (or restart) rendering. */
		_renderParam->StartRender(_renderDelegate->IsBatch());
	}
	else if (_renderParam->SceneEdited())
	{
		/* Push all changes to the scene. */
		_renderParam->SyncRender();
	}

	/* The renderer is now up to date on all changes. */
	_renderParam->ResetSceneEdited();

#if defined(PXR_VERSION) && PXR_VERSION <= 2002
	// Blit, only when no AOVs are specified.
	if (_aovBindings.empty())
	{
		_colorBuffer.Resolve();
		_depthBuffer.Resolve();
		_compositor.UpdateColor(
			_width, _height, _colorBuffer.GetFormat(), _colorBuffer.Map());
		_compositor.UpdateDepth(_width, _height, (uint8_t *)_depthBuffer.Map());
		_colorBuffer.Unmap();
		_depthBuffer.Unmap();
		_compositor.Draw();
	}
#else
	TF_VERIFY(!_aovBindings.empty(), "No aov bindings to render into");
#endif
}

void HdNSIRenderPass::UpdateOutputs(
	const HdRenderPassAovBindingVector &bindings)
{
	NSI::Context &nsi = _renderParam->AcquireSceneForEdit();

	/* Delete the NSI nodes from the previous output specification. */
	for( const std::string &h : _outputNodes )
	{
		nsi.Delete(h);
	}
	_outputNodes.clear();

	int i = 0;
	for( const HdRenderPassAovBinding &aov : bindings )
	{
		/* Create an output layer. */
		std::string layerHandle = Handle("|outputLayer") + std::to_string(i);
		nsi.Create(layerHandle, "outputlayer");
		nsi.SetAttribute(layerHandle, NSI::IntegerArg("sortkey", i));

		/* Have the buffer set some of the attributes. */
		static_cast<HdNSIRenderBuffer*>(aov.renderBuffer)
			->SetNSILayerAttributes(nsi, layerHandle, aov);

		if( aov.aovName == HdAovTokens->depth )
		{
			/* Depth AOV needs extra data for the projection. */
			nsi.SetAttribute(layerHandle,
				NSI::PointerArg("projectdepth", &_depthProj));
		}

		/* Create an output driver. */
		std::string driverHandle = Handle("|outputDriver") + std::to_string(i);
		nsi.Create(driverHandle, "outputdriver");
		nsi.SetAttribute(driverHandle, (
			NSI::StringArg("drivername", "HdNSI"),
			NSI::StringArg("imagefilename", aov.aovName.GetString())));

		/* Connect everything together. */
		nsi.Connect(driverHandle, "", layerHandle, "outputdrivers");
		nsi.Connect(layerHandle, "", ScreenHandle(), "outputlayers");

		/* Record the nodes so we can delete them on the next update. */
		_outputNodes.push_back(layerHandle);
		_outputNodes.push_back(driverHandle);

		++i;
	}
}

std::string HdNSIRenderPass::Handle(const char *suffix) const
{
	return _handlesPrefix + suffix;
}

std::string HdNSIRenderPass::ScreenHandle() const
{
	return Handle("|screen1");
}

void HdNSIRenderPass::SetOversampling() const
{
	NSI::Context &nsi = _renderParam->AcquireSceneForEdit();

	VtValue s = _renderDelegate->GetRenderSetting(
		HdNSIRenderSettingsTokens->pixelSamples);

	_renderParam->StopRender();

	nsi.SetAttribute(ScreenHandle(),
		NSI::IntegerArg("oversampling", s.Get<int>()));
}

std::string HdNSIRenderPass::ExportNSIHeadLightShader()
{
	NSI::Context &nsi = _renderParam->AcquireSceneForEdit();
	std::string handle = Handle("|headlight|shader");
	nsi.Create(handle, "shader");

	NSI::ArgumentList args;

	args.Add(new NSI::StringArg("shaderfilename",
		_renderDelegate->FindShader("UsdLuxLight")));

	VtValue intensity = _renderDelegate->GetRenderSetting(
		HdNSIRenderSettingsTokens->cameraLightIntensity);
	/*
		This ugly mess is because we need the initial value to be a float or
		the UI won't build itself. But said UI then sets any new value as a
		double.
	*/
	float intensity_value = intensity.IsHolding<float>()
		? intensity.Get<float>() : intensity.Get<double>();
	float color_data[3] = { intensity_value, intensity_value, intensity_value };
	args.Add(new NSI::ColorArg("color_", color_data));
	args.Add(new NSI::IntegerArg("normalize_", 1));

	nsi.SetAttribute(handle, args);
	return handle;
}

void HdNSIRenderPass::UpdateHeadlight(
	bool enable,
	const HdNSICamera *camera)
{
	std::string geo_handle = Handle("|headlight|geo");
	std::string attr_handle = Handle("|headlight|attr");

	if (!enable)
	{
		/* Don't mark the scene as edited if we have nothing to do. */
		if (m_headlight_xform.empty())
			return;

		NSI::Context &nsi = _renderParam->AcquireSceneForEdit();
		nsi.Delete(geo_handle);
		nsi.Delete(attr_handle);
		m_headlight_xform.clear();
		return;
	}

	/* Don't mark the scene as edited if we have nothing to do. */
	if (m_headlight_xform == camera->GetTransformNode())
		return;

	NSI::Context &nsi = _renderParam->AcquireSceneForEdit();

	if (m_headlight_xform.empty())
	{
		/* Create geo node. */
		nsi.Create(geo_handle, "environment");
		nsi.SetAttribute(geo_handle, NSI::DoubleArg("angle", 0));
		/* Create attributes node. */
		nsi.Create(attr_handle, "attributes");
		nsi.Connect(attr_handle, "", geo_handle, "geometryattributes");
		/* Attach light shader to geo. */
		std::string headlightShaderHandle = ExportNSIHeadLightShader();
		nsi.Connect(headlightShaderHandle, "", attr_handle, "surfaceshader");
	}
	else
	{
		/* Disconnect from previous camera. */
		nsi.Disconnect(geo_handle, "", m_headlight_xform, "objects");
	}

	/* Connect to the camera's transform. */
	m_headlight_xform = camera->GetTransformNode();
	nsi.Connect(geo_handle, "", m_headlight_xform, "objects");
}

void HdNSIRenderPass::UpdateScreen(
	const HdRenderPassState &renderPassState,
	const HdNSICamera *camera)
{
	NSI::Context &nsi = _renderParam->AcquireSceneForEdit();

	if (!m_screen_created)
	{
		nsi.Create(ScreenHandle(), "screen");
		SetOversampling();
		m_screen_created = true;
	}

	/* Update the connected camera. */
	if (!m_render_camera.empty())
	{
		nsi.Disconnect(ScreenHandle(), "", m_render_camera, "screens");
	}
	m_render_camera = camera->GetCameraNode();
	nsi.Connect(ScreenHandle(), "", m_render_camera, "screens");

	NSI::ArgumentList args;

	/* Resolution. */
	const GfVec4f &vp = renderPassState.GetViewport();
	int res[2] = { int(vp[2]), int(vp[3]) };
	args.Add(NSI::Argument::New("resolution")
		->SetArrayType(NSITypeInteger, 2)
		->CopyValue(res, sizeof(res)));

	/* TODO: crop window */

	double pixel_aspect = _renderDelegate->GetRenderSetting<float>(
		UsdRenderTokens->pixelAspectRatio, 1.0f);

	/*
		Compute the desired image aspect ratio. Do it from the resolution
		UsdRenderSettings, if available. Otherwise, use the viewport.
	*/
	double resolution_aspect;
	VtValue rs_res = _renderDelegate->GetRenderSetting(
		UsdRenderTokens->resolution);
	if (rs_res.IsHolding<GfVec2i>())
	{
		GfVec2i r = rs_res.Get<GfVec2i>();
		resolution_aspect = double(r[0]) / double(r[1]);
	}
	else
	{
		resolution_aspect = vp[2] / vp[3];
	}
	double image_aspect = resolution_aspect * pixel_aspect;

	/* Get camera aperture. */
	GfRange2d ap_range = camera->GetAperture();

	/*
		If we have an aspect ratio policy from UsdRenderSettings, use that. If
		not, use the camera's window policy. Can't the latter just be correct?
		Certainly not. Should all the matching options be named backwards?
		Certainly so. Does this look designed by two completely separate teams?
		Hell yes! Hail Hydra.
	*/
	CameraUtilConformWindowPolicy conform_policy = camera->GetWindowPolicy();
	VtValue arcp = _renderDelegate->GetRenderSetting(
		UsdRenderTokens->aspectRatioConformPolicy);
	if (arcp.IsHolding<TfToken>())
	{
		const TfToken rs_policy = arcp.Get<TfToken>();
		if (rs_policy == UsdRenderTokens->expandAperture)
			conform_policy = CameraUtilFit;
		else if (rs_policy == UsdRenderTokens->cropAperture)
			conform_policy = CameraUtilCrop;
		else if (rs_policy == UsdRenderTokens->adjustApertureWidth)
			conform_policy = CameraUtilMatchVertically;
		else if (rs_policy == UsdRenderTokens->adjustApertureHeight)
			conform_policy = CameraUtilMatchHorizontally;
		else if (rs_policy == UsdRenderTokens->adjustPixelAspectRatio)
			conform_policy = CameraUtilDontConform;
		else
		{
			TF_WARN("Unknown aspectRatioConformPolicy: %s",
				rs_policy.GetText());
		}
	}
	ap_range = CameraUtilConformedWindow(
		ap_range, conform_policy, image_aspect);
	auto ap_min = ap_range.GetMin();
	auto ap_max = ap_range.GetMax();

	/*
		Recompute pixel aspect ratio, for the aspect ratio policy which
		consists of not adjusting the aperture. For every other one, this
		should recompute the same value so there's no harm in leaving it.
	*/
	image_aspect = ap_range.GetSize()[0] / ap_range.GetSize()[1];
	pixel_aspect = image_aspect / resolution_aspect;

	double window_data[2][2] =
	{
		{ ap_min[0], ap_min[1] }, { ap_max[0], ap_max[1] }
	};
	args.Add(NSI::Argument::New("screenwindow")
		->SetArrayType(NSITypeDouble, 2)
		->SetCount(2)
		->CopyValue(window_data, sizeof(window_data)));

	args.Add(new NSI::FloatArg("pixelaspectratio", pixel_aspect));

	nsi.SetAttribute(ScreenHandle(), args);
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
