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

TF_DEFINE_ENV_SETTING(HDNSI_PIXEL_SAMPLES, 8,
        "Samples per pixel before we stop rendering (must be >= 1)");

TF_DEFINE_ENV_SETTING(HDNSI_CAMERA_LIGHT_INTENSITY, "1",
        "Intensity of the camera light.");

TF_DEFINE_ENV_SETTING(HDNSI_ENV_LIGHT_IMAGE, "",
        "File path to the enviroment image.");

TF_DEFINE_ENV_SETTING(HDNSI_ENV_LIGHT_MAPPING, 0,
        "Format of enviroment image, spherical (0) or angular (1)");

TF_DEFINE_ENV_SETTING(HDNSI_ENV_LIGHT_INTENSITY, "1",
        "Intensity of enviroment image");

TF_DEFINE_ENV_SETTING(HDNSI_ENV_AS_BACKGROUND, 1,
        "If display environment image as background");

TF_DEFINE_ENV_SETTING(HDNSI_ENV_USE_SKY, 1,
        "Create 3Delight Sky as environment");

TF_DEFINE_ENV_SETTING(HDNSI_MESH_CLOCKWISEWINDING, -1,
        "Set the clockwisewinding for mesh");

TF_DEFINE_ENV_SETTING(HDNSI_PRINT_CONFIGURATION, 1,
        "Print configuration at startup (values > 0 are true)");

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

    std::string cameraLightIntensityVal =
        TfGetEnvSetting(HDNSI_CAMERA_LIGHT_INTENSITY);
    cameraLightIntensity = atof(cameraLightIntensityVal.c_str());

    envLightPath = TfGetEnvSetting(HDNSI_ENV_LIGHT_IMAGE);

    envLightMapping = TfGetEnvSetting(HDNSI_ENV_LIGHT_MAPPING);

    std::string envLightIntensityVal =
        TfGetEnvSetting(HDNSI_ENV_LIGHT_INTENSITY);
    envLightIntensity = atof(envLightIntensityVal.c_str());

    envAsBackground = TfGetEnvSetting(HDNSI_ENV_AS_BACKGROUND);

    envUseSky = TfGetEnvSetting(HDNSI_ENV_USE_SKY);

    meshClockwisewinding = TfGetEnvSetting(HDNSI_MESH_CLOCKWISEWINDING);

    if (TfGetEnvSetting(HDNSI_PRINT_CONFIGURATION) > 0) {
        std::cout
            << "HdNSI Configuration: \n"
            << "  shadingSamples            = "
            <<    shadingSamples << "\n"
            << "  pixelSamples              = "
            <<    pixelSamples << "\n"
            << "  cameraLightIntensity      = "
            <<    cameraLightIntensity << "\n"
            << "  envLightImage             = "
            <<    envLightPath << "\n"
            << "  envLightMapping           = "
            <<    envLightMapping << "\n"
            << "  envLightIntensity         = "
            <<    envLightIntensity << "\n"
            << "  envAsBackground           = "
            <<    envAsBackground << "\n"
            << "  envUseSky                 = "
            <<    envUseSky << "\n"
            << "  meshClockwisewinding      = "
            << meshClockwisewinding << "\n"
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
