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

TF_DEFINE_PRIVATE_TOKENS(
	_tokens,
	/* This is how Houdini sends non raster render products. */
	(delegateRenderProducts)
	/* Values in the render product map. */
	(productName)
	(productType)
	(orderedVars)
	/* Values in one of the vars. */
	((aovDescriptor_format, "aovDescriptor.format"))
	((aovDescriptor_multiSampled, "aovDescriptor.multiSampled"))
	((aovDescriptor_clearValue, "aovDescriptor.clearValue"))
	((aovDescriptor_aovSettings, "aovDescriptor.aovSettings"))
	/* Types not defined in UsdRenderTokens */
	(vector3f)
	(normal3f)
	((_float, "float"))
	(color4f)
	(float4)
	/* Our custom product types. */
	((nsi_apistream, "nsi:apistream"))
	((nsi_display, "nsi:display"))
	((nsi_exr, "nsi:exr"))
	((nsi_deepexr, "nsi:deepexr"))
	((nsi_deepalphaexr, "nsi:deepalphaexr"))
	((nsi_dwaaexr, "nsi:dwaaexr"))
	((nsi_deepalphadwaaexr, "nsi:deepalphadwaaexr"))
	/* Entries in HdAovSettingsMap. */
	((driver_format, "driver:parameters:aov:format"))
	/* Driver aov formats. */
	(color3h)
	(color4h)
	(half)
	(half2)
	(half3)
	(half4)
	(color3u8)
	(color4u8)
);

namespace
{
typedef TfHashMap<TfToken, VtValue, TfToken::HashFunctor> TokenValueMap;
VtValue GetHashMapEntry(const TokenValueMap &map, const TfToken &name)
{
	auto it = map.find(name);
	if( it == map.end() )
		return {};
	return it->second;
}
}

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
	, m_render_camera({})
{
	static std::atomic<unsigned> pass_counter{0};
	_handlesPrefix = "pass" + std::to_string(++pass_counter);

	m_render_camera.SetId(Handle("renderCam"));
	m_render_camera.SetUseGlobalSettings();
}

HdNSIRenderPass::~HdNSIRenderPass()
{
#if defined(PXR_VERSION) && PXR_VERSION <= 2111
	/* Delete the placeholder cam if one was used. */
	if (m_placeholder_camera)
	{
		m_placeholder_camera->Finalize(_renderParam);
		m_placeholder_camera.reset();
	}
#endif

	/* If still rendering, stop it. */
	if( _renderParam->IsRendering() )
	{
		_renderParam->StopRender();
		_renderParam->Wait();
	}
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

/*
	If there's a nsi stream render product, return its filename.
*/
std::string HdNSIRenderPass::GetAPIStreamProduct(
	HdNSIRenderDelegate *renderDelegate)
{
	VtValue products_val = renderDelegate->GetRenderSetting(
		_tokens->delegateRenderProducts);
	if( !products_val.IsHolding<VtArray<TokenValueMap>>() )
		return {};

	const auto &products = products_val.Get<VtArray<TokenValueMap>>();
	for( const HdRenderSettingsMap &prod : products )
	{
		VtValue productName_val = GetHashMapEntry(prod, _tokens->productName);
		VtValue productType_val = GetHashMapEntry(prod, _tokens->productType);

		if( !productName_val.IsHolding<TfToken>() ||
		    !productType_val.IsHolding<TfToken>() )
		{
			TF_WARN("Bad render product definition");
			continue;
		}

		TfToken productName = productName_val.Get<TfToken>();
		TfToken productType = productType_val.Get<TfToken>();

		if( productType == _tokens->nsi_apistream )
		{
			return productName.GetString();
		}
	}
	return {};
}

void HdNSIRenderPass::_Execute(
	HdRenderPassStateSharedPtr const& renderPassState,
	TfTokenVector const &renderTags)
{
	GfVec4f vp = renderPassState->GetViewport();
	auto *camera = static_cast<const HdNSICamera*>(
		renderPassState->GetCamera());
#if defined(PXR_VERSION) && PXR_VERSION <= 2111
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
#endif
	/*
		If either the viewport, the selected camera
		or the aperture offset changes, update screen.
	*/
	bool force_screen_update = false;
	if (_width != vp[2] || _height != vp[3]
#if defined(PXR_VERSION) && PXR_VERSION >= 2102
	    || _framing != renderPassState->GetFraming()
#endif
	    )
	{
		_width = vp[2];
		_height = vp[3];
#if defined(PXR_VERSION) && PXR_VERSION >= 2102
		_framing = renderPassState->GetFraming();
#endif
		/* Resolution changes require stopping the render. */
		_renderParam->StopRender();
		force_screen_update = true;
	}

	/*
		Update our render camera. Note that 'camera' might be a different camera
		than previously. For some camera changes and also resolution changes,
		update the screen as well.
	*/
	if( m_render_camera.UpdateExportedCamera(camera->Data(), _renderParam) ||
	    m_render_camera.IsNew() || force_screen_update )
	{
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
		ExportRenderProducts();
	}

	/* The output driver needs part of the projection matrix to remap Z. */
	const GfMatrix4d &projMatrix = m_render_camera.GetProjectionMatrix();
	_depthProj.M22 = projMatrix[2][2];
	_depthProj.M32 = projMatrix[3][2];

	/* Enable headlight if there are no lights in the scene. */
	UpdateHeadlight(!_renderParam->HasLights(), camera);

	if (_renderDelegate->HasAPIStreamProduct())
	{
		_renderParam->DoStreamExport();
	}
	else if (!_renderParam->IsRendering())
	{
		/* Start (or restart) rendering. */
		_renderParam->StartRender(_renderDelegate->IsBatch());

		//If rendering started in batch mode, wait for it to finish.
		if (_renderDelegate->IsBatch())
		{
			_renderParam->Wait();
		}
	}
	else if (_renderParam->SceneEdited())
	{
		/* Push all changes to the scene. */
		_renderParam->SyncRender();
	}

	/* The renderer is now up to date on all changes. */
	_renderParam->ResetSceneEdited();
	/* The camera has been hooked up everywhere. */
	m_render_camera.SetUsed();

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

		auto renderBuffer = static_cast<HdNSIRenderBuffer*>(aov.renderBuffer);
		/* The output driver will retrieve this pointer to access the buffer. */
		nsi.SetAttribute(layerHandle, NSI::PointerArg("buffer", renderBuffer));
		/* Set format to match the buffer. */
		SetFormatNSILayerAttributes(
			nsi, layerHandle, renderBuffer->GetFormat(), nullptr);
		/* Set what to produce from raw source or builtin Hydra AOV. */
		if( !SetRawSourceNSILayerAttributes(nsi, layerHandle, aov.aovSettings) )
		{
			renderBuffer->SetBindingNSILayerAttributes(nsi, layerHandle, aov);
		}

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

/*
	This is an extension of UpdateOutputs() for offline renders.

	It handles render outputs which are not of the "raster" type. Meaning
	images not going through the usual HdRenderBuffer. This can be output to
	3Delight Display, to a deepexr file, etc.
*/
void HdNSIRenderPass::ExportRenderProducts()
{
	VtValue products_val = _renderDelegate->GetRenderSetting(
		_tokens->delegateRenderProducts);
	if( !products_val.IsHolding<VtArray<TokenValueMap>>() )
		return;

	NSI::Context &nsi = _renderParam->AcquireSceneForEdit();
	const auto &products = products_val.Get<VtArray<TokenValueMap>>();
	int i = 0;
	for( const HdRenderSettingsMap &prod : products )
	{
		VtValue productName_val = GetHashMapEntry(prod, _tokens->productName);
		VtValue productType_val = GetHashMapEntry(prod, _tokens->productType);
		VtValue orderedVars_val = GetHashMapEntry(prod, _tokens->orderedVars);

		if( !productName_val.IsHolding<TfToken>() ||
		    !productType_val.IsHolding<TfToken>() ||
		    !orderedVars_val.IsHolding<VtArray<TokenValueMap>>() )
		{
			TF_WARN("Bad render product definition");
			continue;
		}

		TfToken productName = productName_val.Get<TfToken>();
		TfToken productType = productType_val.Get<TfToken>();
		const VtArray<TokenValueMap> orderedVars =
			orderedVars_val.Get<VtArray<TokenValueMap>>();

		std::vector<HdAovDescriptor> descriptors;
		for( const auto &var : orderedVars )
		{
			/*
				Build HdAovDescriptor from aovDescriptor.* values. Has
				duplicates of other values so it's all we really need.
			*/
			HdAovDescriptor desc;
			VtValue format =
				GetHashMapEntry(var, _tokens->aovDescriptor_format);
			VtValue multiSampled =
				GetHashMapEntry(var, _tokens->aovDescriptor_multiSampled);
			desc.clearValue =
				GetHashMapEntry(var, _tokens->aovDescriptor_clearValue);
			VtValue aovSettings =
				GetHashMapEntry(var, _tokens->aovDescriptor_aovSettings);

			if( !format.IsHolding<HdFormat>() ||
			    !multiSampled.IsHolding<bool>() ||
			    !aovSettings.IsHolding<HdAovSettingsMap>() )
			{
				TF_WARN("Bad aovDescriptor in render product ordered vars");
				continue;
			}

			desc.format = format.Get<HdFormat>();
			desc.multiSampled = multiSampled.Get<bool>();
			desc.aovSettings = aovSettings.Get<HdAovSettingsMap>();

			descriptors.emplace_back(std::move(desc));
		}

		if( descriptors.empty() )
			continue;

		std::string drivername;
		if( productType == _tokens->nsi_display )
			drivername = "idisplay";
		else if(
			productType == _tokens->nsi_exr ||
			productType == _tokens->nsi_deepexr ||
			productType == _tokens->nsi_deepalphaexr ||
			productType == _tokens->nsi_dwaaexr ||
			productType == _tokens->nsi_deepalphadwaaexr )
		{
			drivername = productType.GetString().substr(4);
		}
		else
			continue; /* ignore unknown and nsi:apistream */

		/* Create a single output driver for all the layers of a product. */
		std::string driverHandle =
			Handle("|productOutputDriver") + std::to_string(i);
		nsi.Create(driverHandle, "outputdriver");
		nsi.SetAttribute(driverHandle, (
			NSI::StringArg("drivername", drivername),
			NSI::StringArg("imagefilename", productName.GetString())));
		/* Record those even if there shouldn't be updates for now. */
		_outputNodes.push_back(driverHandle);

		for( const HdAovDescriptor &desc : descriptors )
		{
			/* Create an output layer. */
			std::string layerHandle =
				Handle("|productOutputLayer") + std::to_string(i);
			nsi.Create(layerHandle, "outputlayer");
			nsi.SetAttribute(layerHandle, NSI::IntegerArg("sortkey", i));

			/* Set the attributes specific to this outut. */
			SetFormatNSILayerAttributes(
				nsi, layerHandle, desc.format, &desc.aovSettings);
			SetRawSourceNSILayerAttributes(nsi, layerHandle, desc.aovSettings);

			/* Connect with output driver and screen. */
			nsi.Connect(driverHandle, "", layerHandle, "outputdrivers");
			nsi.Connect(layerHandle, "", ScreenHandle(), "outputlayers");

			/* Record those even if there shouldn't be updates for now. */
			_outputNodes.push_back(layerHandle);

			++i;
		}
	}
}

/*
	This handles UsdRenderVar defined from Houdini for offline rendering.
	Normally not used for a regular Hydra viewer.
*/
bool HdNSIRenderPass::SetRawSourceNSILayerAttributes(
	NSI::Context &nsi,
	const std::string &layerHandle,
	const HdAovSettingsMap &aov)
{
	auto sourceType = GetHashMapEntry(aov, UsdRenderTokens->sourceType);
	if( sourceType != UsdRenderTokens->raw )
		return false;

	VtValue sourceName = GetHashMapEntry(aov, UsdRenderTokens->sourceName);
	std::string name;
	if( sourceName.IsHolding<std::string>() )
	{
		name = sourceName.Get<std::string>();
		/* Parse any source prefix which might be in the name. */
		const std::string sources[] =
			{"shader:", "builtin:", "attribute:"};
		std::string source = "shader";

		for( const std::string &s : sources )
		{
			if( 0 == name.compare(0, s.size(), s) )
			{
				source = s.substr(0, s.size() - 1);
				name = name.substr(s.size());
				break;
			}
		}

		nsi.SetAttribute(layerHandle, (
			NSI::StringArg("variablename", name),
			NSI::StringArg("variablesource", source)));

		if( name == "Ci" || name == "outlines" )
		{
			nsi.SetAttribute(layerHandle, NSI::IntegerArg("drawoutlines", 1));
		}
	}

	VtValue dataType = GetHashMapEntry(aov, UsdRenderTokens->dataType);
	if( dataType == UsdRenderTokens->color3f )
	{
		nsi.SetAttribute(layerHandle,
			NSI::StringArg("layertype", "color"));
	}
	else if( dataType == _tokens->vector3f || dataType == _tokens->normal3f )
	{
		nsi.SetAttribute(layerHandle,
			NSI::StringArg("layertype", "vector"));
	}
	else if( dataType == _tokens->_float )
	{
		nsi.SetAttribute(layerHandle,
			NSI::StringArg("layertype", "scalar"));
	}
	else if( dataType == _tokens->color4f || dataType == _tokens->float4 )
	{
		if( name == "outlines" )
		{
			nsi.SetAttribute(layerHandle,
				NSI::StringArg("layertype", "quad"));
		}
		else
		{
			/* Should probably fix 3Delight so 'quad' always works. */
			nsi.SetAttribute(layerHandle, (
				NSI::StringArg("layertype", "color"),
				NSI::IntegerArg("withalpha", 1)));
		}
	}

	return true;
}

/**
	\param aovSettings
		Optional aov settings where we first look for Houdini's
		driver:parameters:aov:format. This is needed because unlike for raster
		AOVs where the HdFormat is based on that parameter, it is based on the
		dataType field for extra delegate products.
*/
void HdNSIRenderPass::SetFormatNSILayerAttributes(
	NSI::Context &nsi,
	const std::string &layerHandle,
	HdFormat format,
	const HdAovSettingsMap *aovSettings)
{
	if( aovSettings )
	{
		VtValue format = GetHashMapEntry(*aovSettings, _tokens->driver_format);
		if( format.IsHolding<TfToken>() )
		{
			TfToken f = format.Get<TfToken>();
			if( f == _tokens->color3h || f == _tokens->color4h ||
			    f == _tokens->half || f == _tokens->half2 ||
			    f == _tokens->half3 || f == _tokens->half4 )
			{
				nsi.SetAttribute(layerHandle,
					NSI::StringArg("scalarformat", "half"));
			}
			else if( f == _tokens->color3u8 || f == _tokens->color4u8 )
			{
				nsi.SetAttribute(layerHandle, (
					NSI::StringArg("scalarformat", "uint8"),
					NSI::IntegerArg("dithering", 1)));
			}
			else
			{
				nsi.SetAttribute(layerHandle,
					NSI::StringArg("scalarformat", "float"));
			}
			return;
		}
	}

	HdFormat componentFormat = HdGetComponentFormat(format);

	if( componentFormat == HdFormatFloat32 ||
	    componentFormat == HdFormatInt32 )
	{
		/* Integers are output as float and converted in the output driver. */
		nsi.SetAttribute(layerHandle, NSI::StringArg("scalarformat", "float"));
	}
	else if( componentFormat == HdFormatFloat16 )
	{
		nsi.SetAttribute(layerHandle, NSI::StringArg("scalarformat", "half"));
	}
	else if( componentFormat == HdFormatUNorm8 )
	{
		nsi.SetAttribute(layerHandle, (
			NSI::StringArg("scalarformat", "uint8"),
			NSI::IntegerArg("dithering", 1)));
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
	if (m_headlight_xform == m_render_camera.GetTransformNode() &&
	    !m_render_camera.IsNew())
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
	m_headlight_xform = m_render_camera.GetTransformNode();
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

	/* Connect screen to the render camera if it is a new node. */
	if( m_render_camera.IsNew() )
	{
		nsi.Connect(
			ScreenHandle(), "",
			m_render_camera.GetCameraNode(), "screens");
	}

	NSI::ArgumentList args;

	/* Resolution and its aspect ratio. */
	int res[2];
	double resolution_aspect;
	/* Pixel aspect ratio. */
	double pixel_aspect;

#if defined(PXR_VERSION) && PXR_VERSION >= 2102
	const CameraUtilFraming &framing = renderPassState.GetFraming();
	if( framing.IsValid() )
	{
		/* TODO: handle data window for crop and overscan */
		GfVec2f resolution = framing.displayWindow.GetSize();
		res[0] = int(resolution[0]);
		res[1] = int(resolution[1]);
		resolution_aspect = double(resolution[0] / resolution[1]);
		pixel_aspect = framing.pixelAspectRatio;
	}
	else
	/* fallback on old API if framing was not set. */
#endif
	{
		/* Resolution. */
		const GfVec4f &vp = renderPassState.GetViewport();
		res[0] = int(vp[2]);
		res[1] = int(vp[3]);

		/*
			Use resolution UsdRenderSettings, if available. Otherwise, use the
			viewport. Houdini's USD Render needs this for correct framing, or
			at least used to.
		*/
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

		pixel_aspect = _renderDelegate->GetRenderSetting<float>(
			UsdRenderTokens->pixelAspectRatio, 1.0f);
	}

	/* Don't output this unless it actually changes or 3Delight will be much
	   slower */
	if( m_screen_resolution[0] != res[0] || m_screen_resolution[1] != res[1] )
	{
		m_screen_resolution[0] = res[0];
		m_screen_resolution[1] = res[1];
		args.Add(NSI::Argument::New("resolution")
			->SetArrayType(NSITypeInteger, 2)
			->CopyValue(res, sizeof(res)));
	}

	/* Compute the desired image aspect ratio. */
	double image_aspect = resolution_aspect * pixel_aspect;

	/* Get camera aperture. */
	GfRange2d ap_range = m_render_camera.GetAperture();

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
