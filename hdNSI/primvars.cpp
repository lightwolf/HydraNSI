#include "primvars.h"

#include <pxr/base/gf/vec2f.h>

#include <nsi_dynamic.hpp>

PXR_NAMESPACE_OPEN_SCOPE

void HdNSIPrimvars::Sync(
	HdSceneDelegate *sceneDelegate,
	HdNSIRenderParam *renderParam,
	HdDirtyBits *dirtyBits,
	NSI::Context &nsi,
	const SdfPath &primId,
	const std::string &geoHandle,
	const VtIntArray &vertexIndices )
{
	if (0 == (*dirtyBits & HdChangeTracker::DirtyPrimvar))
		return;

	HdInterpolation types[] =
	{
		HdInterpolationConstant,
		HdInterpolationUniform,
		HdInterpolationVarying,
		HdInterpolationVertex,
		HdInterpolationFaceVarying
	};

	for (auto type : types)
	{
		HdPrimvarDescriptorVector primvars =
			sceneDelegate->GetPrimvarDescriptors(primId, type);

		for (const HdPrimvarDescriptor &primvar : primvars)
		{
			/* These are not done here. For now. */
			if (primvar.name == HdTokens->points ||
			    primvar.name == HdTokens->normals ||
			    primvar.name == HdTokens->widths)
			{
				continue;
			}

			/* Ignore the ones starting with '__' for now. Specifically, we
			   have no need for __faceindex on subdivs. */
			if (primvar.name.GetString().substr(0, 2) == "__")
				continue;

			SetOnePrimvar(
				sceneDelegate, nsi, primId, geoHandle, vertexIndices, primvar);
		}
	}

	*dirtyBits &= ~HdDirtyBits(HdChangeTracker::DirtyPrimvar);
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
}

bool HdNSIPrimvars::SetAttributeFromValue(
	NSI::Context &nsi,
	const std::string &nodeHandle,
	const HdPrimvarDescriptor &primvar,
	const VtValue &value,
	int flags)
{
	const std::string &argName = primvar.name.GetString();

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

void HdNSIPrimvars::SetOnePrimvar(
	HdSceneDelegate *sceneDelegate,
	NSI::Context &nsi,
	const SdfPath &primId,
	const std::string &geoHandle,
	const VtIntArray &vertexIndices,
	const HdPrimvarDescriptor &primvar)
{
	/*
		Get() magically applies the USD primvar indices. SamplePrimvar() does
		not, which makes it fairly useless here.
	*/
	VtValue v = sceneDelegate->Get(primId, primvar.name);
	if (v.IsEmpty())
		return;

	int flags = 0;
	if (primvar.interpolation == HdInterpolationVarying)
	{
		flags |= NSIParamInterpolateLinear;
	}

	if (!SetAttributeFromValue(nsi, geoHandle, primvar, v, flags))
		return;

	/* Output indices if needed. */
	if (primvar.interpolation == HdInterpolationVertex && !vertexIndices.empty())
	{
		nsi.SetAttribute(geoHandle,
			*NSI::Argument(primvar.name.GetString() + ".indices")
			.SetType(NSITypeInteger)
			->SetCount(vertexIndices.size())
			->SetValuePointer(vertexIndices.cdata()));
	}
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
