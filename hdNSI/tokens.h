#ifndef HDNSI_TOKENS_H
#define HDNSI_TOKENS_H

#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/hd/api.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

#define HDNSI_SETTINGS_TOKENS \
	((shadingSamples, "nsi:global:shadingsamples")) \
	((volumeSamples, "nsi:global:volumesamples")) \
	((pixelSamples, "nsi:global:pixelsamples")) \
	((maximumDiffuseDepth, "nsi:global:maximumdiffusedepth")) \
	((maximumReflectionDepth, "nsi:global:maximumreflectiondepth")) \
	((maximumRefractionDepth, "nsi:global:maximumrefractiondepth")) \
	((maximumHairDepth, "nsi:global:maximumhairdeph")) \
	((maximumDistance, "nsi:global:maximumdistance")) \
	(cameraLightIntensity)

TF_DECLARE_PUBLIC_TOKENS(
	HdNSIRenderSettingsTokens, HDNSI_SETTINGS_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
