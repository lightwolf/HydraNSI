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
#include "pxr/imaging/hdNSI/config.h"

#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/instantiateSingleton.h"

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

// Instantiate the config singleton.
TF_INSTANTIATE_SINGLETON(HdNSIConfig);

// Each configuration variable has an associated environment variable.
// The environment variable macro takes the variable name, a default value,
// and a description...
TF_DEFINE_ENV_SETTING(HDNSI_SHADING_SAMPLES, 64,
        "Shading samples (must be >= 1)");

TF_DEFINE_ENV_SETTING(HDNSI_PIXEL_SAMPLES, 2,
        "Samples per pixel before we stop rendering (must be >= 1)");

TF_DEFINE_ENV_SETTING(HDNSI_CAMERA_LIGHT_INTENSITY, 100,
        "Intensity of the camera light, specified as a percentage of <1,1,1>.");

TF_DEFINE_ENV_SETTING(HDNSI_PRINT_CONFIGURATION, 0,
        "Should HdNSI print configuration on startup? (values > 0 are true)");

HdNSIConfig::HdNSIConfig()
{
    // We need DELIGHT environment variable.
    char *env = getenv("DELIGHT");
    if (env) {
        delight = std::string(env);
    }

    // Read in values from the environment, clamping them to valid ranges.
    shadingSamples = std::max(1,
            TfGetEnvSetting(HDNSI_SHADING_SAMPLES));
    pixelSamples = std::max(1,
            TfGetEnvSetting(HDNSI_PIXEL_SAMPLES));
    cameraLightIntensity = (std::max(100,
            TfGetEnvSetting(HDNSI_CAMERA_LIGHT_INTENSITY)) / 100.0f);

    if (TfGetEnvSetting(HDNSI_PRINT_CONFIGURATION) > 0) {
        std::cout
            << "HdNSI Configuration: \n"
            << "  shadingSamples            = "
            <<    shadingSamples << "\n"
            << "  pixelSamples              = "
            <<    pixelSamples << "\n"
            << "  cameraLightIntensity      = "
            <<    cameraLightIntensity   << "\n"
            ;
    }
}

/*static*/
const HdNSIConfig&
HdNSIConfig::GetInstance()
{
    return TfSingleton<HdNSIConfig>::GetInstance();
}

PXR_NAMESPACE_CLOSE_SCOPE
