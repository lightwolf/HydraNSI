//
// Copyright 2018 Pixar
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
#include "pxr/imaging/hdNSI/renderBuffer.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/base/gf/half.h"

PXR_NAMESPACE_OPEN_SCOPE

HdNSIRenderBuffer::HdNSIRenderBuffer(SdfPath const& id)
    : HdRenderBuffer(id)
    , _width(0)
    , _height(0)
    , _format(HdFormatInvalid)
    , _buffer()
    , _mappers(0)
    , _converged(false)
{
}

HdNSIRenderBuffer::~HdNSIRenderBuffer()
{
}

void HdNSIRenderBuffer::Sync(
    HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam,
    HdDirtyBits *dirtyBits)
{
    if (0 != (*dirtyBits & DirtyDescription))
    {
        auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
        /* Stop the render so it does not write to a deleted buffer. */
        nsiRenderParam->StopRender();
        /* Record that we changed something. */
        nsiRenderParam->AcquireSceneForEdit();
    }

    HdRenderBuffer::Sync(sceneDelegate, renderParam, dirtyBits);
}

void HdNSIRenderBuffer::Finalize(HdRenderParam *renderParam)
{
    auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
    /* Stop the render so it does not write to a deleted buffer. */
    nsiRenderParam->StopRender();
    /* Record that we changed something. */
    nsiRenderParam->AcquireSceneForEdit();

    HdRenderBuffer::Finalize(renderParam);
}

void HdNSIRenderBuffer::_Deallocate()
{
    // If the buffer is mapped while we're doing this, there's not a great
    // recovery path...
    TF_VERIFY(!IsMapped());

    _width = 0;
    _height = 0;
    _format = HdFormatInvalid;
    _buffer.resize(0);

    _mappers.store(0);
    _converged.store(false);
}

bool HdNSIRenderBuffer::Allocate(
    GfVec3i const& dimensions,
    HdFormat format,
    bool multiSampled)
{
    _Deallocate();

    if (dimensions[2] != 1) {
        TF_WARN("Render buffer allocated with dims <%d, %d, %d> and"
                " format %s; depth must be 1!",
                dimensions[0], dimensions[1], dimensions[2],
                TfEnum::GetName(format).c_str());
        return false;
    }

    _width = dimensions[0];
    _height = dimensions[1];
    _format = format;
    _buffer.resize(_width * _height * HdDataSizeOfFormat(format));

    return true;
}

uint8_t* HdNSIRenderBuffer::Map()
{
    ++_mappers;
    return _buffer.data();
}

void HdNSIRenderBuffer::Unmap()
{
    --_mappers;
}

bool HdNSIRenderBuffer::IsMapped() const
{
    return _mappers.load() != 0;
}

void HdNSIRenderBuffer::Resolve()
{
}

void HdNSIRenderBuffer::SetNSILayerAttributes(
	NSI::Context &nsi,
	const std::string &layerHandle,
	TfToken aovName) const
{
	nsi.SetAttribute(layerHandle,
		NSI::PointerArg("buffer", this));

    HdFormat componentFormat = HdGetComponentFormat(_format);

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

	if( aovName == HdAovTokens->color )
	{
		nsi.SetAttribute(layerHandle, (
            NSI::StringArg("variablename", "Ci"),
            NSI::StringArg("layertype", "color"),
            NSI::IntegerArg("withalpha", 1),
            NSI::StringArg("variablesource", "shader")));
	}
	else if( aovName == HdAovTokens->depth ||
             aovName == HdAovTokens->linearDepth )
	{
		nsi.SetAttribute(layerHandle, (
            NSI::StringArg("variablename", "z"),
            NSI::StringArg("layertype", "scalar"),
            NSI::StringArg("filter", "min"),
            NSI::DoubleArg("filterwidth", 1.0)));
	}
    else if( aovName == HdAovTokens->normal )
    {
		nsi.SetAttribute(layerHandle, (
            NSI::StringArg("variablename", "N.world"),
            NSI::StringArg("layertype", "vector"),
            NSI::StringArg("variablesource", "builtin")));
    }
    else if( aovName == HdAovTokens->Neye )
    {
		nsi.SetAttribute(layerHandle, (
            NSI::StringArg("variablename", "N.camera"),
            NSI::StringArg("layertype", "vector"),
            NSI::StringArg("variablesource", "builtin")));
    }
    else if( aovName == HdAovTokens->primId ||
             aovName == HdAovTokens->instanceId ||
             aovName == HdAovTokens->elementId )
    {
		nsi.SetAttribute(layerHandle, (
            NSI::StringArg("variablename", aovName.GetString()),
            NSI::StringArg("variablesource", "attribute"),
            NSI::StringArg("layertype", "scalar"),
            NSI::FloatArg("backgroundvalue", -1.0f),
            NSI::StringArg("filter", "zmin"),
            NSI::DoubleArg("filterwidth", 1.0)));
    }
    else
    {
        HdParsedAovToken aovId(aovName);
        if (aovId.isPrimvar)
        {
            nsi.SetAttribute(layerHandle, (
                NSI::StringArg("variablename", aovId.name.GetString()),
                NSI::StringArg("variablesource", "attribute"),
                NSI::StringArg("layertype", "color")));
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
