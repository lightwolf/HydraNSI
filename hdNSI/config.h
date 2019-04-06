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
#ifndef HDNSI_CONFIG_H
#define HDNSI_CONFIG_H

#include <string>

#include "pxr/pxr.h"
#include "pxr/base/arch/library.h"
#include "pxr/base/tf/singleton.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdNSIConfig
///
/// This class is a singleton, holding configuration parameters for HdNSI.
/// Everything is provided with a default, but can be overridden using
/// environment variables before launching a hydra process.
///
/// Many of the parameters can be used to control quality/performance
/// tradeoffs, or to alter how HdNSI takes advantage of parallelism.
///
/// At startup, this class will print config parameters if
/// *HDNSI_PRINT_CONFIGURATION* is true. Integer values greater than zero
/// are considered "true".
///
class HdNSIConfig {
public:
    /// \brief Return the configuration singleton.
    static const HdNSIConfig &GetInstance();

    /// 3Delight DELIGHT environment variable.
    ///
    std::string delight;

private:
    // The constructor initializes the config variables with their
    // default or environment-provided override, and optionally prints
    // them.
    HdNSIConfig();
    ~HdNSIConfig() = default;

    HdNSIConfig(const HdNSIConfig&) = delete;
    HdNSIConfig& operator=(const HdNSIConfig&) = delete;

    friend class TfSingleton<HdNSIConfig>;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDNSI_CONFIG_H
// vim: set softtabstop=4 expandtab shiftwidth=4:
