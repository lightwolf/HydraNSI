#include "primvars.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/imaging/hd/extComputationUtils.h>

#include <nsi_dynamic.hpp>

PXR_NAMESPACE_OPEN_SCOPE

/**
	FIXME: when a primvar is deleted, we don't restore the default value.
	This can be tested easily by changing visibilkity to camera to off
	and then removing the attribute. The object will remain invisible.
*/
void HdNSIPrimvars::Sync(
	HdSceneDelegate *sceneDelegate,
	HdNSIRenderParam *renderParam,
	HdDirtyBits *dirtyBits,
	NSI::Context &nsi,
	const SdfPath &primId,
	const std::string &geoHandle,
	const VtIntArray &vertexIndices )
{
	auto primvar_bits =
		HdChangeTracker::DirtyPoints |
		HdChangeTracker::DirtyPrimvar |
		HdChangeTracker::DirtyNormals |
		HdChangeTracker::DirtyWidths;

	if (0 == (*dirtyBits & primvar_bits))
		return;

	HdInterpolation types[] =
	{
		HdInterpolationConstant,
		HdInterpolationUniform,
		HdInterpolationVarying,
		HdInterpolationVertex,
		HdInterpolationFaceVarying,
		HdInterpolationInstance
	};

	if (0 != (*dirtyBits & HdChangeTracker::DirtyNormals))
	{
		m_has_normals = false;
	}

	for (auto type : types)
	{
		HdPrimvarDescriptorVector primvars =
			sceneDelegate->GetPrimvarDescriptors(primId, type);

		for (const HdPrimvarDescriptor &primvar : primvars)
		{
			if (!ShouldUpdateVar(*dirtyBits, primId, primvar.name))
				continue;

			const std::string &primvar_name = primvar.name.GetString();

			/* Ignore the ones starting with '__' for now. Specifically, we
			   have no need for __faceindex on subdivs. */
			if (primvar_name.substr(0, 2) == "__")
				continue;

			if( primvar_name.find("nsi:object:") == 0 )
			{
				/* This is object-level attributes */
				SetObjectAttributes(
					sceneDelegate, nsi, primId, geoHandle, primvar);
			}
			else
			{
				SampleArray v;
				sceneDelegate->SamplePrimvar(primId, primvar.name, &v);
				SetOnePrimvar(
					sceneDelegate, nsi, primId, geoHandle, vertexIndices,
					primvar, v);
			}
		}

		HdExtComputationPrimvarDescriptorVector compvars =
			sceneDelegate->GetExtComputationPrimvarDescriptors(primId, type);

		HdExtComputationPrimvarDescriptorVector dirty_comp;
		for (const HdExtComputationPrimvarDescriptor &primvar : compvars)
		{
			if (ShouldUpdateVar(*dirtyBits, primId, primvar.name))
			{
				dirty_comp.emplace_back(primvar);
			}
		}

		if (!dirty_comp.empty() )
		{
			const HdExtComputationUtils::ValueStore valueStore =
				HdExtComputationUtils::GetComputedPrimvarValues(
					dirty_comp, sceneDelegate);

			for (const HdExtComputationPrimvarDescriptor &primvar : dirty_comp)
			{
				auto value_it = valueStore.find(primvar.name);
				if (value_it == valueStore.end())
					continue;

				SetOnePrimvar(
					sceneDelegate, nsi, primId, geoHandle, vertexIndices,
					primvar, value_it->second);
			}
		}
	}

	*dirtyBits &= ~HdDirtyBits(primvar_bits);
}

namespace
{
NSIType_t RoleTo3fType(const TfToken &role)
{
	if (role == HdPrimvarRoleTokens->vector)
		return NSITypeVector;
	if (role == HdPrimvarRoleTokens->normal)
		return NSITypeNormal;
	if (role == HdPrimvarRoleTokens->point)
		return NSITypePoint;
	/* HdPrimvarRoleTokens->color, also default. */
	return NSITypeColor;
}

/*
	Convert USD primvar name to NSI attribute name.
*/
std::string TokenToAttName(const TfToken &token)
{
	if (token == HdTokens->points)
		return "P";
	if (token == HdTokens->normals)
		return "N";
	if (token == HdTokens->widths)
		return "width";
	return token.GetString();
}
}

/**
	\param nsi
		The nsi context to use.
	\param nodeHandle
		The nsi node handle to set the attribute for.
	\param primvar
		The primvar description of the attribute to set.
	\param value
		The value to set the attribute to.
	\param flags
		The nsi flags to use when setting the attribute.
	\param sample_time
		The time at which the value should be set.
	\param use_time
		If true, we set the attribute for a specific time. If false, the
		attribute is set without a time value and applies for all times.
	\returns
		true on success, false if the attribute has a type we could not handle.
*/
bool HdNSIPrimvars::SetAttributeFromValue(
	NSI::Context &nsi,
	const std::string &nodeHandle,
	const HdPrimvarDescriptor &primvar,
	const VtValue &value,
	int flags,
	double sample_time,
	bool use_time)
{
	const std::string &argName = TokenToAttName(primvar.name);

	if (value.IsHolding<TfToken>())
	{
		nsi.SetAttribute(nodeHandle, NSI::StringArg(argName,
			value.Get<TfToken>().GetString()));
	}
	else if (value.IsHolding<std::string>())
	{
		nsi.SetAttribute(nodeHandle, NSI::StringArg(argName,
			value.Get<std::string>()));
	}
	else if (value.IsHolding<VtArray<float>>())
	{
		const auto &v_array = value.Get<VtArray<float>>();
		NSI::Argument arg(argName);
		arg.SetType(NSITypeFloat);
		arg.SetCount(v_array.size());
		arg.SetFlags(flags);
		arg.SetValuePointer(v_array.cdata());
		if (use_time)
		{
			nsi.SetAttributeAtTime(nodeHandle, sample_time, arg);
		}
		else
		{
			nsi.SetAttribute(nodeHandle, arg);
		}
	}
	else if (value.IsHolding<VtArray<GfVec2f>>())
	{
		const auto &v_array = value.Get<VtArray<GfVec2f>>();
		NSI::Argument arg(argName);
		arg.SetArrayType(NSITypeFloat, 2);
		arg.SetCount(v_array.size());
		arg.SetFlags(flags);
		arg.SetValuePointer(v_array.cdata()->data());
		if (use_time)
		{
			nsi.SetAttributeAtTime(nodeHandle, sample_time, arg);
		}
		else
		{
			nsi.SetAttribute(nodeHandle, arg);
		}
	}
	else if (value.IsHolding<VtArray<GfVec3f>>())
	{
		const auto &v_array = value.Get<VtArray<GfVec3f>>();
		NSI::Argument arg(argName);
		arg.SetType(RoleTo3fType(primvar.role));
		arg.SetCount(v_array.size());
		arg.SetFlags(flags);
		arg.SetValuePointer(v_array.cdata()->data());
		if (use_time)
		{
			nsi.SetAttributeAtTime(nodeHandle, sample_time, arg);
		}
		else
		{
			nsi.SetAttribute(nodeHandle, arg);
		}
	}
	else if (value.IsHolding<int>())
	{
		nsi.SetAttribute(nodeHandle, NSI::IntegerArg(argName,
			value.Get<int>()));
	}
	else
	{
		return false;
	}
	return true;
}

/**
	\returns
		true if a specific primvar should be processed.
*/
bool HdNSIPrimvars::ShouldUpdateVar(
	HdDirtyBits dirtyBits,
	const SdfPath &id,
	const TfToken &var) const
{
	/* Check the skip list. */
	for( const TfToken &t : m_skip )
	{
		if (t == var)
			return false;
	}
	/* Only process the dirty ones. */
	return HdChangeTracker::IsPrimvarDirty(dirtyBits, id, var);
}

/**
	\brief Deal with USD primvars that translate to NSI attributes
	(e.g. visibility attributes)
*/
void HdNSIPrimvars::SetObjectAttributes(
	HdSceneDelegate *sceneDelegate,
	NSI::Context &nsi,
	const SdfPath &primId,
	const std::string &geoHandle,
	const HdPrimvarDescriptor &primvar)
{
	if( primvar.name.GetString().find("nsi:object:visibility_") == 0 )
	{
		SetVisibilityAttributes(
			sceneDelegate, nsi, primId, geoHandle, primvar );
	}
}

/*
	We transform USD attributes, that look like this:
		"nsi:object:visibillity_camera"
	to this:
		"visibility.camera"

	To set them directly on an NSI attributes node.
*/
void HdNSIPrimvars::SetVisibilityAttributes(
	HdSceneDelegate *sceneDelegate,
	NSI::Context &nsi,
	const SdfPath &primId,
	const std::string &geoHandle,
	const HdPrimvarDescriptor &primvar)
{
	VtValue v = sceneDelegate->Get(primId, primvar.name);
	if( v.IsEmpty() || !v.IsHolding<bool>() )
	{
		assert( false );
		return;
	}

	/*  construct nsi attribute name */
	std::string nsi_name = primvar.name.GetString();
	nsi_name = nsi_name.c_str() + strlen("nsi:object:");
	std::replace( nsi_name.begin(), nsi_name.end(), '_', '.');

	/* create an attribute node and set the attribute. re-creating the same
	attribute again and again is not a problem in NSI. */
	std::string attribute_handle = geoHandle + "|visibility_attributes";
	nsi.Create( attribute_handle, "attributes" );
	nsi.SetAttribute( attribute_handle, NSI::IntegerArg(nsi_name, v.Get<bool>()) );
	nsi.Connect( attribute_handle, "", geoHandle, "geometryattributes" );
}

void HdNSIPrimvars::SetOnePrimvar(
	HdSceneDelegate *sceneDelegate,
	NSI::Context &nsi,
	const SdfPath &primId,
	const std::string &geoHandle,
	const VtIntArray &vertexIndices,
	const HdPrimvarDescriptor &primvar,
	const SampleArray &values)
{
	bool has_motion = values.count > 1;
	if (has_motion)
	{
		/* Delete previous motion samples so we don't add to them. */
		nsi.DeleteAttribute(geoHandle, TokenToAttName(primvar.name));
	}
	for (size_t i = 0; i < values.count; ++i )
	{
		const VtValue &value = values.values[i];
		if (value.IsEmpty())
			return;

		/* Track if we export normals. */
		m_has_normals = m_has_normals || primvar.name == HdTokens->normals;
		/* Hold onto points if requested. */
		if (m_keep_points && primvar.name == HdTokens->points &&
			value.IsHolding<VtVec3fArray>())
		{
			m_points = value.Get<VtVec3fArray>();
		}

		int flags = 0;
		if (primvar.interpolation == HdInterpolationVarying)
		{
			flags |= NSIParamInterpolateLinear;
		}

		if (!SetAttributeFromValue(
				nsi, geoHandle, primvar, value, flags,
				values.times[i], has_motion))
		{
			return;
		}
	}

	/* Output indices if needed. */
	if (primvar.interpolation == HdInterpolationVertex && !vertexIndices.empty())
	{
		nsi.SetAttribute(geoHandle,
			*NSI::Argument(TokenToAttName(primvar.name) + ".indices")
			.SetType(NSITypeInteger)
			->SetCount(vertexIndices.size())
			->SetValuePointer(vertexIndices.cdata()));
	}
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
