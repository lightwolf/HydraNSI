#ifndef HDNSI_COMPATIBILITY_H
#define HDNSI_COMPATIBILITY_H

#include <pxr/pxr.h>

/*
	These make compatibility across USD commit 6772ff6ab650027f somewhat
	easier. It removed the instancerId parameter from the rprim constructors.
*/
#if defined(PXR_VERSION) && PXR_VERSION <= 2011
#   define DECLARE_IID ,SdfPath const& instancerId
#   define PASS_IID ,instancerId
#else
#   define DECLARE_IID
#   define PASS_IID
#endif

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
