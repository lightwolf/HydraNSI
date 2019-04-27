#include "pxr/imaging/hdNSI/primvars.h"

#include "nsi_dynamic.hpp"
#include "pxr/base/gf/vec2f.h"

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
	const std::string &argName = primvar.name.GetString();

	if (v.IsHolding<TfToken>())
	{
		nsi.SetAttribute(geoHandle, NSI::StringArg(argName,
			v.Get<TfToken>().GetString()));
	}
	else if (v.IsHolding<VtArray<float>>())
	{
		const auto &v_array = v.Get<VtArray<float>>();
		nsi.SetAttribute(geoHandle, *NSI::Argument(argName)
			.SetType(NSITypeFloat)
			->SetCount(v_array.size())
			->SetFlags(flags)
			->SetValuePointer(v_array.cdata()));
	}
	else if (v.IsHolding<VtArray<GfVec2f>>())
	{
		const auto &v_array = v.Get<VtArray<GfVec2f>>();
		nsi.SetAttribute(geoHandle, *NSI::Argument(argName)
			.SetArrayType(NSITypeFloat, 2)
			->SetCount(v_array.size())
			->SetFlags(flags)
			->SetValuePointer(v_array.cdata()->data()));
	}
	else if (v.IsHolding<VtArray<GfVec3f>>())
	{
		const auto &v_array = v.Get<VtArray<GfVec3f>>();
		nsi.SetAttribute(geoHandle, *NSI::Argument(argName)
			.SetType(RoleTo3fType(primvar.role))
			->SetCount(v_array.size())
			->SetFlags(flags)
			->SetValuePointer(v_array.cdata()->data()));
	}
	else if (v.IsHolding<int>())
	{
		nsi.SetAttribute(geoHandle, NSI::IntegerArg(argName, v.Get<int>()));
	}
	else
	{
		return;
	}

	/* Output indices if needed. */
	bool supportsIndices =
		primvar.interpolation == HdInterpolationVarying ||
		primvar.interpolation == HdInterpolationVertex ||
		primvar.interpolation == HdInterpolationFaceVarying;
	if (supportsIndices && !vertexIndices.empty())
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
