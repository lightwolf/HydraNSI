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
#ifndef HDNSI_RENDERBUFFER_H
#define HDNSI_RENDERBUFFER_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"

#include <nsi.hpp>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderBuffer : public HdRenderBuffer
{
public:
    HdNSIRenderBuffer(SdfPath const& id);
    ~HdNSIRenderBuffer();

    virtual void Sync(
        HdSceneDelegate *sceneDelegate,
        HdRenderParam *renderParam,
        HdDirtyBits *dirtyBits) override;

    /// Deallocate before deletion.
    virtual void Finalize(HdRenderParam *renderParam) override;

    virtual bool Allocate(
        GfVec3i const& dimensions,
        HdFormat format,
        bool multiSampled) override;

    virtual unsigned int GetWidth() const override { return _width; }
    virtual unsigned int GetHeight() const override { return _height; }
    virtual unsigned int GetDepth() const override { return 1; }
    virtual HdFormat GetFormat() const override { return _format; }

    /*
        This appears unused by Hydra. While we do multisample, we don't do it
        using Hydra's definition so it probably makes more sense to return
        false here, in case it is ever used somewhere.
    */
    virtual bool IsMultiSampled() const override { return false; }

    virtual uint8_t* Map() override;
    virtual void Unmap() override;

    virtual bool IsMapped() const override;

    virtual bool IsConverged() const override { return _converged.load(); }
    void SetConverged(bool cv) { _converged.store(cv); }

    virtual void Resolve() override;

    void SetNSILayerAttributes(
        NSI::Context &nsi,
        const std::string &layerHandle,
        TfToken aovName) const;

private:
    // Release any allocated resources.
    virtual void _Deallocate() override;

    // Buffer width.
    unsigned int _width;
    // Buffer height.
    unsigned int _height;
    // Buffer format.
    HdFormat _format;

    // The resolved output buffer.
    std::vector<uint8_t> _buffer;

    // The number of callers mapping this buffer.
    std::atomic<int> _mappers;
    // Whether the buffer has been marked as converged.
    std::atomic<bool> _converged;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDNSI_RENDERBUFFER_H
// vim: set softtabstop=4 expandtab shiftwidth=4:
