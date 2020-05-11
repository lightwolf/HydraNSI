//
// Copyright 2017 Pixar
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
#ifndef HDNSI_RENDER_PARAM_H
#define HDNSI_RENDER_PARAM_H

#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/pxr.h>

#include <nsi_dynamic.hpp>

#include <atomic>
#include <cassert>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderDelegate;

///
/// \class HdNSIRenderParam
///
/// The render delegate can create an object of type HdRenderParam, to pass
/// to each prim during Sync(). HdNSI uses this class to pass top-level
/// NSI state around.
/// 
class HdNSIRenderParam final : public HdRenderParam {
public:
	HdNSIRenderParam(
		HdNSIRenderDelegate *renderDelegate,
		const std::shared_ptr<NSI::Context> &nsi)
	: _renderDelegate(renderDelegate)
	, _nsi(nsi)
	, _sceneEdited(false)
	, _numLights{0}
	{}

	virtual ~HdNSIRenderParam() = default;

	HdNSIRenderDelegate* GetRenderDelegate() const { return _renderDelegate; }

	/// Accessor for the top-level NSI scene.
	NSI::Context& AcquireSceneForEdit()
	{
		_sceneEdited.store(true, std::memory_order_relaxed);
		return *_nsi;
	}
	/// Accessor for the global shared NSI context.
	NSI::Context& GetNSIContext() { return *_nsi; }

	bool SceneEdited() const { return _sceneEdited; }
	void ResetSceneEdited() { _sceneEdited = false; }

	bool IsConverged() const { return _isConverged; }

	void AddLight() { ++_numLights; }
	void RemoveLight() { --_numLights; }
	bool HasLights() const { return _numLights != 0; }

	bool IsRendering() const { return _rendering; }

	void StartRender()
	{
		assert(!_rendering);
		_rendering = true;
		GetNSIContext().RenderControl((
			NSI::CStringPArg("action", "start"),
			NSI::PointerArg("stoppedcallback", (void*)StatusCB),
			NSI::PointerArg("stoppedcallbackdata", this),
			NSI::IntegerArg("interactive", 1),
			NSI::IntegerArg("progressive", 1)));
	}

	void StopRender()
	{
		if (_rendering)
		{
			_rendering = false;
			GetNSIContext().RenderControl(
				NSI::CStringPArg("action", "stop"));
		}
	}

	void SyncRender()
	{
		GetNSIContext().RenderControl(
			NSI::CStringPArg("action", "synchronize"));
	}

private:
	static void StatusCB(void *data, NSIContext_t ctx, int status)
	{
		auto param = (HdNSIRenderParam*)data;
		if (status == NSIRenderSynchronized)
			param->_isConverged = true;
		if (status == NSIRenderRestarted)
			param->_isConverged = false;
	}

private:
	HdNSIRenderDelegate *_renderDelegate;

	/// A smart pointer to the NSI API.
	std::shared_ptr<NSI::Context> _nsi;

	/// true when the render is actually running
	bool _rendering{false};

	/// True when the render buffers are fully in sync with the scene.
	bool _isConverged{false};

	/// A flag to know if the scene has been edited.
	std::atomic<bool> _sceneEdited;

	/// Number of lights in the scene.
	std::atomic<unsigned> _numLights;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDNSI_RENDER_PARAM_H
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
