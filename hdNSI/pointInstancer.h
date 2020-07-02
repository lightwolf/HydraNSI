#ifndef HDNSI_POINTINSTANCER_H
#define HDNSI_POINTINSTANCER_H

#include "primvars.h"

#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/sceneDelegate.h>

#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderParam;

/*
	This class handles point instancer primitives.

	The way this works in Hydra is a little awkward. The instancers are not
	first class primitives (eg. they don't have Sync() and Finalize() methods).
	Rather, they act as a sort of common point of communication for the
	multiple prototype primitives which might use said instancer. This is
	likely another kludge inherited from the needs of the GL renderer.

	The way we handle this is what we progressively build the list of models as
	we become aware of the prototype primitives which use a given instancer. It
	may waste some space by going through some intermediate states but it makes
	other things (eg. primvar export) simpler to have a single instancer with
	all the primitives.
*/
class HdNSIPointInstancer : public HdInstancer
{
public:
	HdNSIPointInstancer(
		HdSceneDelegate *sceneDelegate,
		const SdfPath &id,
		const SdfPath &parentInstancerId);

	~HdNSIPointInstancer() override;

	void Destroy(
		HdNSIRenderParam *renderParam);

	void SyncPrototype(
		HdNSIRenderParam *renderParam,
		const SdfPath &prototypeId,
		const std::string &prototypeHandle,
		bool isNewPrototype);

private:
	/* Handle of transform node. */
	std::string m_xformHandle;
	/* Handle of instancer node. */
	std::string m_instancerHandle;
	/* List of prototypes using this instancer. */
	std::vector<SdfPath> m_prototype_ids;
	/* Index of the prototype used by each instance. */
	std::vector<int> m_prototype_indices;
	/* The instanceId attribute (for AOV). */
	std::vector<int> m_instance_id;

	HdNSIPrimvars m_primVars;

	/* Because this can be used by multiple prototypes in ||, we need some
	   locking. */
	std::mutex m_mutex;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
