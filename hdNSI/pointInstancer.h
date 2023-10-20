#ifndef HDNSI_POINTINSTANCER_H
#define HDNSI_POINTINSTANCER_H

#include "compatibility.h"
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

	As of 21.02, instancers do have Sync() and Finalize() but Sync() is still
	called from the prototypes. Finalize() is correctly hooked up though.

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
		const SdfPath &id
		DECLARE_IID);

	~HdNSIPointInstancer() override;

#if defined(PXR_VERSION) && PXR_VERSION <= 2011
	void Destroy(
		HdNSIRenderParam *renderParam);
#else
	/*
		No implementation of Sync() from USD commit
		18e7193d7c47005fa79407bcad9b4b51cc3a31bf yet. The reason is that it
		does essentially the same thing we already do in SyncPrototype(), only
		with part of it in _SyncInstancerAndParents(). It should be simple
		enough to migrate to that once the Sync() calls are no longer done from
		the model rprims. And perhaps by then we can drop support for the older
		USD versions.
	*/

	void Finalize(HdRenderParam *renderParam) override;

	/* HdInstancer's GetInitialDirtyBitsMask() is ok. */
#endif

	void SyncPrototype(
		HdNSIRenderParam *renderParam,
		const SdfPath &prototypeId,
		bool isNewPrototype);

private:
	std::string ModelHandle(int i) const;

private:
	/* Handle of transform node. */
	std::string m_xformHandle;
	/* Handle of instancer node. */
	std::string m_instancerHandle;
	/* List of prototypes using this instancer. */
	std::vector<SdfPath> m_prototype_ids;
	/* Number of distinct models assembled from the prototypes. */
	int m_model_count{0};
	/* Index of the model used by each instance. */
	std::vector<int> m_model_indices;
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
