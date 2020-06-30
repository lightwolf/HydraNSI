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
		HdInterpolationFaceVarying
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
				VtValue v = sceneDelegate->Get(primId, primvar.name);
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
			if (HdChangeTracker::IsPrimvarDirty(
					*dirtyBits, primId, primvar.name))
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

bool HdNSIPrimvars::SetAttributeFromValue(
	NSI::Context &nsi,
	const std::string &nodeHandle,
	const HdPrimvarDescriptor &primvar,
	const VtValue &value,
	int flags)
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
		nsi.SetAttribute(nodeHandle, *NSI::Argument(argName)
			.SetType(NSITypeFloat)
			->SetCount(v_array.size())
			->SetFlags(flags)
			->SetValuePointer(v_array.cdata()));
	}
	else if (value.IsHolding<VtArray<GfVec2f>>())
	{
		const auto &v_array = value.Get<VtArray<GfVec2f>>();
		nsi.SetAttribute(nodeHandle, *NSI::Argument(argName)
			.SetArrayType(NSITypeFloat, 2)
			->SetCount(v_array.size())
			->SetFlags(flags)
			->SetValuePointer(v_array.cdata()->data()));
	}
	else if (value.IsHolding<VtArray<GfVec3f>>())
	{
		const auto &v_array = value.Get<VtArray<GfVec3f>>();
		nsi.SetAttribute(nodeHandle, *NSI::Argument(argName)
			.SetType(RoleTo3fType(primvar.role))
			->SetCount(v_array.size())
			->SetFlags(flags)
			->SetValuePointer(v_array.cdata()->data()));
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
	const VtValue value)
{
	if (value.IsEmpty())
		return;

	/* Track of we export normals. */
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

	if (!SetAttributeFromValue(nsi, geoHandle, primvar, value, flags))
		return;

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
