#include "pointInstancer.h"

#include "renderParam.h"
#include "rprimBase.h"

#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quath.h>

PXR_NAMESPACE_OPEN_SCOPE

#if defined(PXR_VERSION) && PXR_VERSION <= 1911
/* Instancer tokens were not in 19.11 yet. So make them up. */
TF_DEFINE_PRIVATE_TOKENS(
	HdInstancerTokens,
	(instanceTransform)
	(scale)
	(rotate)
	(translate)
);
/* Same for this method of HdInstancer which returns them. */
namespace
{
const TfTokenVector& GetBuiltinPrimvarNames()
{
	static const TfTokenVector primvarNames =
	{
		HdInstancerTokens->instanceTransform,
		HdInstancerTokens->rotate,
		HdInstancerTokens->scale,
		HdInstancerTokens->translate
	};
	return primvarNames;
}
}
#endif

HdNSIPointInstancer::HdNSIPointInstancer(
	HdSceneDelegate *sceneDelegate,
	const SdfPath &id
	DECLARE_IID) /* Parent instancer id, if an instancer is instanced. */
:
	HdInstancer{sceneDelegate, id PASS_IID},
	m_primVars{false}
{
	/* Don't output the transform primvars as actual primvars. */
	m_primVars.SetSkipVars(GetBuiltinPrimvarNames());
}

HdNSIPointInstancer::~HdNSIPointInstancer()
{
	assert(m_xformHandle.empty());
	assert(m_instancerHandle.empty());
}

/**
	\brief Remove the instancer from the NSI scene.
*/
#if defined(PXR_VERSION) && PXR_VERSION <= 2011
void HdNSIPointInstancer::Destroy(
	HdNSIRenderParam *nsiRenderParam)
{
#else
void HdNSIPointInstancer::Finalize(
	HdRenderParam *renderParam)
{
	auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
#endif
	if (!m_instancerHandle.empty())
	{
		NSI::Context &nsi = nsiRenderParam->AcquireSceneForEdit();
		nsi.Delete(m_instancerHandle);
		nsi.Delete(m_xformHandle);
		for( int i = 0; i < m_model_count; ++i )
		{
			nsi.Delete(ModelHandle(i));
		}
		m_instancerHandle.clear();
		m_xformHandle.clear();
	};
}

/**
	\brief Sync a prototype to this instancer.

	\param renderParam
		The render param.
	\param prototypeId
		The prototype's Id.
	\param isNewPrototype
		true iff it is the first time this prototype does a Sync.
*/
void HdNSIPointInstancer::SyncPrototype(
	HdNSIRenderParam *renderParam,
	const SdfPath &prototypeId,
	bool isNewPrototype)
{
	std::lock_guard<std::mutex> instancer_l(m_mutex);

	HdRenderIndex &renderIndex = GetDelegate()->GetRenderIndex();
	HdChangeTracker &changeTracker = renderIndex.GetChangeTracker();
	const SdfPath &id = GetId();

	NSI::Context &nsi = renderParam->AcquireSceneForEdit();
	HdDirtyBits dirtyBits = changeTracker.GetInstancerDirtyBits(id);

	if (m_instancerHandle.empty())
	{
		/* Create the instancer and its transform node. */
		m_xformHandle = HdNSIRprimBase::HandleFromId(GetId());
		m_instancerHandle = m_xformHandle + "|geo";
		nsi.Create(m_xformHandle, "transform");
		nsi.Create(m_instancerHandle, "instances");
		nsi.Connect(m_instancerHandle, "", m_xformHandle, "objects");
		const SdfPath &parent = GetParentId();
		if (parent.IsEmpty())
		{
			/* No parent instancer. Add to the scene. */
			nsi.Connect(m_xformHandle, "", NSI_SCENE_ROOT, "objects");
		}
		else
		{
			/* Add ourselves as a prototype for the parent instancer. */
			auto instancer = static_cast<HdNSIPointInstancer*>(
				renderIndex.GetInstancer(parent));
			instancer->SyncPrototype(renderParam, id, true);
		}
	}

#if defined(PXR_VERSION) && PXR_VERSION >= 2105
	/*
		Grab all the prototypes at once. This is more efficient as it lets us
		output the right modelindices arrays the first time around instead of
		producing a different one as each prototype is added.
	*/
	SdfPathVector prototypes = GetDelegate()->GetInstancerPrototypes(id);
	if( prototypes != m_prototype_ids )
	{
		m_prototype_ids = prototypes;
		/* Force refresh of model indices. */
		dirtyBits |= HdChangeTracker::DirtyInstanceIndex;
		/*
			Create the transform nodes for prototypes, in case they haven't
			been synchronized yet, which is quite likely for all but the one
			which called us. If they have, it won't hurt.
		*/
		for (const SdfPath &pid : m_prototype_ids)
		{
			nsi.Create(HdNSIRprimBase::HandleFromId(pid), "transform");
		}
	}
#else
	if (isNewPrototype )
	{
		/*
			See if the prototype is already in the list. This could happen
			because we never remove them from that list.
		*/
		unsigned pIdx;
		for (pIdx = 0; pIdx < m_prototype_ids.size(); ++pIdx)
		{
			if (m_prototype_ids[pIdx] == prototypeId)
				break;
		}
		/* If it's a new one, add it. */
		if (pIdx == m_prototype_ids.size())
		{
			m_prototype_ids.emplace_back(prototypeId);
		}

		/* Force refresh of model indices. */
		dirtyBits |= HdChangeTracker::DirtyInstanceIndex;
	}
#endif

	/*
		Here, we attempt to rebuild USD instancing indices from Hydra's
		scrambled idea of what instancing should be like.

		Hydra has no way of grouping several pieces of geometry together for
		instancing. Instead, they will show up as separate prototypes but each
		have the same instance indices array. I think this (being equal) is the
		only case where instance indices arrays overlap. The code below depends
		on this to make reconstruction easier so hopefully it's true. If it
		isn't, a lot more complexity will be needed as models could then be
		multiple permutations of the available prototypes.
	*/
	bool write_modelindices = false;
	if (0 != (dirtyBits & HdChangeTracker::DirtyInstanceIndex))
	{
		/* Delete previous model nodes. */
		for (int i = 0; i < m_model_count; ++i)
		{
			nsi.Delete(ModelHandle(i));
		}
		/* m_model_count should track model_instance_indices.size() */
		m_model_count = 0;
		std::vector<VtIntArray> model_instance_indices;

		/* Assemble prototypes into models. */
		for (size_t p = 0; p < m_prototype_ids.size(); ++p)
		{
			VtIntArray instanceIndices =
				GetDelegate()->GetInstanceIndices(GetId(), m_prototype_ids[p]);
			/* Look for a model with a matching array of instance indices. */
			size_t m;
			for (m = 0; m < model_instance_indices.size(); ++m)
			{
				if (model_instance_indices[m] == instanceIndices)
					break;
			}
			std::string model_handle = ModelHandle(m);
			if (m == model_instance_indices.size())
			{
				/* This is a new model. Create its node. */
				nsi.Create(model_handle, "transform");
				/* Connect it to the instancer. */
				nsi.Connect(
					model_handle, "",
					m_instancerHandle, "sourcemodels",
					NSI::IntegerArg("index", m));
				/* Keep its instance indices. */
				model_instance_indices.emplace_back(std::move(instanceIndices));
				++m_model_count;
			}
			/* Connect the prototype to the model node. */
			nsi.Connect(
				HdNSIRprimBase::HandleFromId(m_prototype_ids[p]), "",
				model_handle, "objects");
		}

		/* Update model indices and instanceId */
		m_model_indices.clear();
		m_instance_id.clear();

		for (size_t m = 0; m < model_instance_indices.size(); ++m)
		{
			const VtIntArray &instanceIndices = model_instance_indices[m];
			for (unsigned i = 0; i < instanceIndices.size(); ++i)
			{
				int idx = instanceIndices[i];
				if (idx >= m_model_indices.size() )
				{
					/* Fill with -1 as there may be gaps in the final array if
					   there are disabled instances. -1 will render nothing. */
					m_model_indices.resize(idx + 1, -1);
					/* The -1 here should not actually be used. */
					m_instance_id.resize(idx + 1, -1);
				}
				m_model_indices[idx] = m;
				m_instance_id[idx] = i;
			}
		}

		/* Delay write a little as the array might still grow. */
		write_modelindices = true;
	}

	/*
		Do the instance xforms. These are primvars but excluded from the
		generic primvar output as they need to be folded into a single matrix
		per instance. They also need to come before generic primvars as those
		clear the dirty bit.
	*/
	if (0 != (dirtyBits & HdChangeTracker::DirtyPrimvar))
	{
		/* First, fetch all the transform related primvars. */
		TfToken xform_tokens[] =
		{
#if defined(PXR_VERSION) && PXR_VERSION < 2311
			HdInstancerTokens->instanceTransform,
			HdInstancerTokens->scale,
			HdInstancerTokens->rotate,
			HdInstancerTokens->translate,
#else
			HdInstancerTokens->instanceTransforms,
			HdInstancerTokens->instanceScales,
			HdInstancerTokens->instanceRotations,
			HdInstancerTokens->instanceTranslations,
#endif
		};
		HdTimeSampleArray<VtValue, 4> xform_primvars[4];

		unsigned num_transforms = 0;
		TfSmallVector<float, 10> times;

		HdPrimvarDescriptorVector primvars = GetDelegate()
			->GetPrimvarDescriptors(id, HdInterpolationInstance);
		for (const HdPrimvarDescriptor &primvar : primvars)
		{
			for (unsigned i = 0; i < 4; ++i)
			{
				if (primvar.name == xform_tokens[i])
				{
					auto &samples = xform_primvars[i];
					GetDelegate()->SamplePrimvar(id, primvar.name, &samples);
					times.insert(times.end(),
						samples.times.data(),
						samples.times.data() + samples.count);
					if (samples.count != 0)
						num_transforms = samples.values[0].GetArraySize();
				}
			}
		}

		/* Compute set of unique time samples. */
		std::sort(times.begin(), times.end());
		times.erase(std::unique(times.begin(), times.end()), times.end());

		if (times.size() > 1)
		{
			/* Delete previous time samples. */
			nsi.DeleteAttribute(m_instancerHandle, "transformationmatrices");
		}

		/* Unbox the values from VtValue to actual type. */
		HdTimeSampleArray<VtMatrix4dArray, 4> pv_transform;
		HdTimeSampleArray<VtVec3fArray, 4> pv_scale;
		HdTimeSampleArray<VtQuathArray, 4> pv_rotate;
		HdTimeSampleArray<VtVec3fArray, 4> pv_translate;
		pv_transform.UnboxFrom(xform_primvars[0]);
		pv_scale.UnboxFrom(xform_primvars[1]);
		pv_rotate.UnboxFrom(xform_primvars[2]);
		pv_translate.UnboxFrom(xform_primvars[3]);

		/* For each time sample, concatenate the vars into a single matrix. */
		VtMatrix4dArray transforms{num_transforms};
		for (float t : times)
		{
			/* Start with a bunch of identity matrices. */
			std::fill_n(transforms.begin(), num_transforms, GfMatrix4d(1.0));
			GfMatrix4d m;
			/* Apply instanceTransform. */
			if (pv_transform.count != 0)
			{
				VtMatrix4dArray values = pv_transform.Resample(t);
				for (unsigned i = 0; i < num_transforms; ++i)
				{
					transforms[i] *= values[i];
				}
			}
			/* Apply scale. */
			if (pv_scale.count != 0)
			{
				VtVec3fArray values = pv_scale.Resample(t);
				for (unsigned i = 0; i < num_transforms; ++i)
				{
					transforms[i] *= m.SetScale(values[i]);
				}
			}
			/* Apply rotate. */
			if (pv_rotate.count != 0)
			{
				VtQuathArray values = pv_rotate.Resample(t);
				for (unsigned i = 0; i < num_transforms; ++i)
				{
					auto v = values[i];
					transforms[i] *= m.SetRotate(v);
				}
			}
			/* Apply translate. */
			if (pv_translate.count != 0)
			{
				VtVec3fArray values = pv_translate.Resample(t);
				for (unsigned i = 0; i < num_transforms; ++i)
				{
					transforms[i] *= m.SetTranslate(GfVec3d(values[i]));
				}
			}

			NSI::Argument arg("transformationmatrices");
			arg.SetType(NSITypeDoubleMatrix);
			arg.SetCount(transforms.size());
			arg.SetValuePointer(transforms.data());
			if (times.size() > 1)
			{
				nsi.SetAttributeAtTime(m_instancerHandle, t, arg);
			}
			else
			{
				nsi.SetAttribute(m_instancerHandle, arg);
			}
		}

		/* If the last instance if disabled, the indices array will be too
		   short. Enlarge it here. */
		if (m_model_indices.size() < num_transforms)
		{
			m_model_indices.resize(num_transforms, -1);
			m_instance_id.resize(num_transforms, -1);
			write_modelindices = true;
		}
	}

	if (write_modelindices)
	{
		nsi.SetAttribute(m_instancerHandle,
			*NSI::Argument("modelindices")
			.SetType(NSITypeInteger)
			->SetCount(m_model_indices.size())
			->SetValuePointer(m_model_indices.data()));

		/* This is for the instanceId AOV. */
		nsi.SetAttribute(m_instancerHandle,
			*NSI::Argument("instanceId")
			.SetType(NSITypeInteger)
			->SetCount(m_instance_id.size())
			->SetValuePointer(m_instance_id.data()));
	}

	/* This handles the single instancer transform. */
	if (0 != (dirtyBits & HdChangeTracker::DirtyTransform))
	{
		HdNSIRprimBase::ExportTransform(
			GetDelegate(), id, true, nsi, m_xformHandle);
	}

	/* Do the primvars. */
	m_primVars.Sync(
		GetDelegate(), renderParam, &dirtyBits, nsi, id, m_instancerHandle, {});

	/* Mark instancer as clean. */
	changeTracker.MarkInstancerClean(id);
}

/**
	\returns
		The handle of the transform node under which we assemble the i-th model.
*/
std::string HdNSIPointInstancer::ModelHandle(int i) const
{
	return m_instancerHandle + "|model_" + std::to_string(i);
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
