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

	static HdDirtyBits ProcessedDirtyBits()
	{
		return HdDirtyBits(HdChangeTracker::Clean
			| HdChangeTracker::DirtyCategories
			| HdChangeTracker::DirtyPrimID
			| HdChangeTracker::DirtyTransform
			| HdChangeTracker::DirtyVisibility
			);
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
		bool isInstancer,
		NSI::Context &nsi,
		const std::string &handle);

	static void ExportTransform(
		const HdTimeSampleArray<GfMatrix4d, 4> &samples,
		NSI::Context &nsi,
		const std::string &handle);

	static bool SameTransform(
		const HdTimeSampleArray<GfMatrix4d, 4> &a,
		const HdTimeSampleArray<GfMatrix4d, 4> &b);

	/* This is the handle by which instancers will grab an rprim. So rprims
	   should be defined under that. */
	static std::string HandleFromId(const SdfPath &id)
		{ return id.GetString(); }

private:
	void Create(
		NSI::Context &nsi,
		const HdRprim &rprim);

	/* NSI node type for the geo. */
	std::string _nodeType;

	/* NSI handles. */
	std::string _masterShapeHandle;
	std::string _xformHandle;
	std::string _attrsHandle;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
