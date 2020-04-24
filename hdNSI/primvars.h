#ifndef HDNSI_PRIMVARS_H
#define HDNSI_PRIMVARS_H

#include <pxr/imaging/hd/sceneDelegate.h>

#include <nsi.hpp>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderParam;

/*
	This class handles primvar export on an rprim.
*/
class HdNSIPrimvars
{
public:
	void Sync(
		HdSceneDelegate *sceneDelegate,
		HdNSIRenderParam *renderParam,
		HdDirtyBits *dirtyBits,
		NSI::Context &nsi,
		const SdfPath &primId,
		const std::string &geoHandle,
		const VtIntArray &vertexIndices );

	static bool SetAttributeFromValue(
		NSI::Context &nsi,
		const std::string &nodeHandle,
		const HdPrimvarDescriptor &primvar,
		const VtValue &value,
		int flags);

private:
	void SetOnePrimvar(
		HdSceneDelegate *sceneDelegate,
		NSI::Context &nsi,
		const SdfPath &primId,
		const std::string &geoHandle,
		const VtIntArray &vertexIndices,
		const HdPrimvarDescriptor &primvar);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
