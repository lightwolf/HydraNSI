#ifndef HDNSI_RPRIMBASE_h
#define HDNSI_RPRIMBASE_h

#include "renderParam.h"

#include <pxr/imaging/hd/sceneDelegate.h>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

/*
	This class handles basic node setup common to all renderable primitive
	types. This includes instancing.
*/
class HdNSIRprimBase
{
public:
	HdNSIRprimBase(const std::string &nodeType)
	:
		_nodeType(nodeType)
	{
	}

	void Sync(
		HdSceneDelegate *sceneDelegate,
		HdNSIRenderParam *renderParam,
		HdDirtyBits *dirtyBits,
		const HdRprim &rprim);

	void Finalize(HdNSIRenderParam *renderParam);

	const std::string& Shape() const { return _masterShapeHandle; }
	const std::string& Attrs() const { return _attrsHandle; }

	static void ExportTransform(
		HdSceneDelegate *sceneDelegate,
		const SdfPath &id,
		NSI::Context &nsi,
		const std::string &handle);

private:
	void Create(
		NSI::Context &nsi,
		const HdRprim &rprim);

	void CheckPrimvars(
		HdSceneDelegate *sceneDelegate,
		HdDirtyBits *dirtyBits,
		const HdRprim &rprim);

	/* NSI node type for the geo. */
	std::string _nodeType;

	/* NSI handles. */
	std::string _masterShapeHandle;
	std::string _xformHandle;
	std::string _instancesHandle;
	std::string _attrsHandle;

	bool _firstSync{true};
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
