#ifndef HDNSI_TOKENS_H
#define HDNSI_TOKENS_H

#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/hd/api.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

#define HDNSI_SETTINGS_TOKENS \
    (shadingSamples) \
    (volumeSamples) \
    (pixelSamples) \
    (cameraLightIntensity) \
    (envLightPath) \
    (envLightMapping) \
    (envLightIntensity) \
    (envAsBackground) \
    (envUseSky)

TF_DECLARE_PUBLIC_TOKENS(
    HdNSIRenderSettingsTokens, HDNSI_SETTINGS_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=4 expandtab shiftwidth=4:
