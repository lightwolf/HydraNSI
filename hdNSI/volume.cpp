#include "volume.h"

#include "field.h"

#include <pxr/usd/sdf/assetPath.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
	/* This is in usdVolImaging but we shouldn't be using it directly. */
    (openvdbAsset)
);

HdNSIVolume::HdNSIVolume(
	const SdfPath &id
	DECLARE_IID)
:
	HdVolume{id PASS_IID},
	m_base{"volume"}
{
}

HdDirtyBits HdNSIVolume::GetInitialDirtyBitsMask() const
{
	int mask = HdChangeTracker::Clean
		| HdChangeTracker::InitRepr
		| HdChangeTracker::DirtyPrimID
		| HdChangeTracker::DirtyTransform
		| HdChangeTracker::DirtyVisibility
		//| HdChangeTracker::DirtyPrimvar // none for now
		| HdChangeTracker::DirtyInstancer
		| HdChangeTracker::DirtyInstanceIndex
		| HdChangeTracker::DirtyMaterialId
		;

	return (HdDirtyBits)mask;
}

void HdNSIVolume::Sync(
	HdSceneDelegate *sceneDelegate,
	HdRenderParam *renderParam,
	HdDirtyBits *dirtyBits,
	const TfToken &reprName)
{
	/* Pull top-level NSI state out of the render param. */
	auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
	NSI::Context &nsi = nsiRenderParam->AcquireSceneForEdit();

	/* The base rprim class tracks this but does not update it itself. */
	if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, GetId()))
	{
		_UpdateVisibility(sceneDelegate, dirtyBits);
	}
#if PXR_VERSION > 2011
	_UpdateInstancer(sceneDelegate, dirtyBits);
#endif

	/* This creates the NSI nodes so it comes before other attributes. */
	m_base.Sync(sceneDelegate, nsiRenderParam, dirtyBits, *this);

	m_material.Sync(
		sceneDelegate, nsiRenderParam, dirtyBits, nsi, GetId(),
		m_base.Shape());

	/*
		It's not clear that this depends on any specific dirty bits. We might
		as well always update it as it's fairly cheap. I hope.
	*/
	m_fields = sceneDelegate->GetVolumeFieldDescriptors(GetId());
	for( const HdVolumeFieldDescriptor &field : m_fields )
	{
		if (field.fieldPrimType == _tokens->openvdbAsset)
		{
			/*
				Just fetch the path directly from here instead of going
				through the HdNSIField object. It's a lot less trouble.
			*/
			VtValue path_v = sceneDelegate->Get(
				field.fieldId, HdFieldTokens->filePath);
			if (path_v.IsHolding<SdfAssetPath>())
			{
				std::string path = path_v.Get<SdfAssetPath>().GetResolvedPath();
				nsi.SetAttribute(m_base.Shape(),
					NSI::StringArg("vdbfilename", path));
			}
			/* No point in setting this more than once. */
			break;
		}
	}

	if( m_materialId != m_material.GetMaterialId() )
	{
		/*
			On a change of assigned material, register ourselves with the new
			material for the parameters which need to be on the volume node.
		*/
		{
			auto old_cb = m_volume_callbacks.lock();
			if (old_cb)
			{
				old_cb->erase(this);
			}
		}

		m_materialId = m_material.GetMaterialId();
#if defined(PXR_VERSION) && PXR_VERSION <= 1911
		auto new_mat = static_cast<HdNSIMaterial*>(const_cast<HdSprim*>(
			sceneDelegate->GetRenderIndex().GetSprim(
				HdPrimTypeTokens->material, m_materialId)));
#else
		auto new_mat = static_cast<HdNSIMaterial*>(
			sceneDelegate->GetRenderIndex().GetSprim(
				HdPrimTypeTokens->material, m_materialId));
#endif
		m_volume_callbacks = new_mat->GetVolumeCallbacks();
		auto new_cb = m_volume_callbacks.lock();
		if (new_cb)
		{
			new_cb->locked_insert(this);
		}
		/* Invoke CB manually as we might have missed a previous one. */
		NewVDBNode(nsi, new_mat);
	}

	*dirtyBits = HdChangeTracker::Clean;
}

void HdNSIVolume::Finalize(HdRenderParam *renderParam)
{
	/* Remove ourselves from the material's volume callbacks. */
	auto vol_cb = m_volume_callbacks.lock();
	if (vol_cb)
	{
		vol_cb->locked_erase(this);
	}

	m_base.Finalize(static_cast<HdNSIRenderParam*>(renderParam));
}

HdDirtyBits HdNSIVolume::_PropagateDirtyBits(HdDirtyBits bits) const
{
	return bits;
}

void HdNSIVolume::_InitRepr(
	const TfToken &reprName,
	HdDirtyBits *dirtyBits)
{
}

bool HdNSIVolume::HasField(const TfToken &name) const
{
	for( const HdVolumeFieldDescriptor &field : m_fields )
	{
		if (field.fieldName == name)
			return true;
	}
	return false;
}

/*
	This is a callback from the material when the vdbVolume shader might
	change. This can mean a new material was assigned or simply that some
	parameters changed.

	We use it to grab the material parameters which must be exported on the
	volume node.
*/
void HdNSIVolume::NewVDBNode(
	NSI::Context &nsi,
	HdNSIMaterial *material)
{
	const HdMaterialNode *vdbVolume = material->GetVDBVolume();
	if (!vdbVolume)
		return;

	/*
		To keep this simple, we make the assumption that string parameters are
		grid (field) names and the one float parameter is velocity scale.
		Update if this ever becomes more complex.
	*/
	for( const TfToken &p : HdNSIMaterial::VolumeNodeParameters() )
	{
		bool set = false;
		auto it = vdbVolume->parameters.find(p);
		if (it != vdbVolume->parameters.end())
		{
			const VtValue &v = it->second;
			if (v.IsHolding<std::string>())
			{
				std::string gridname = v.Get<std::string>();
				/* Only set grids which actually exist in the VDB. */
				if (HasField(TfToken{gridname}))
				{
					nsi.SetAttribute(m_base.Shape(),
						NSI::StringArg(p.GetString(), gridname));
					set = true;
				}
			}
			else if (v.IsHolding<float>())
			{
				/* For velocityscale which is double. */
				nsi.SetAttribute(m_base.Shape(),
					NSI::DoubleArg(p.GetString(), v.Get<float>()));
				set = true;
			}
		}

		/* When updating, delete any value which is no longer set. */
		if (!set)
		{
			nsi.DeleteAttribute(m_base.Shape(), p.GetString());
		}
	}
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
