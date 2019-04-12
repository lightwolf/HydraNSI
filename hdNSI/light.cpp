#include "pxr/imaging/hdNSI/light.h"

#include "pxr/imaging/hd/changeTracker.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hdNSI/renderDelegate.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/usd/usdLux/blackbody.h"
#include "pxr/usd/usdLux/tokens.h"

#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

HdNSILight::HdNSILight(
	const TfToken &typeId,
	const SdfPath &sprimId)
:
	HdLight{sprimId},
	m_typeId{typeId},
	m_nodes_created{false}
{
}

void HdNSILight::Sync(
	HdSceneDelegate *sceneDelegate,
	HdRenderParam *renderParam,
	HdDirtyBits *dirtyBits)
{
    auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
	NSI::Context &nsi = nsiRenderParam->AcquireSceneForEdit();

	std::string xform_handle = GetId().GetString();
	std::string geo_handle = xform_handle + "|geo";

	if (!m_nodes_created)
	{
		CreateNodes(nsiRenderParam, nsi);
	}

	if (0 != (*dirtyBits & DirtyTransform))
	{
		/* sceneDelegate->GetTransform() does not work on lights. */
		constexpr int kMaxSamples = 4;
		GfMatrix4d trs[kMaxSamples];
		float t[kMaxSamples];
		size_t n = sceneDelegate->SampleTransform(
			GetId(), kMaxSamples, &t[0], &trs[0]);
		for (size_t i = 0; i < n; ++i)
		{
			nsi.SetAttributeAtTime(xform_handle, t[i],
				NSI::DoubleMatrixArg("transformationmatrix", trs[i].data()));
		}
	}

	if (0 != (*dirtyBits & DirtyParams))
	{
		SetShaderParams(nsi, sceneDelegate);

		if (m_typeId == HdPrimTypeTokens->diskLight)
		{
			float radius = sceneDelegate->GetLightParamValue(
				GetId(), UsdLuxTokens->radius).Get<float>();
			nsi.SetAttribute(geo_handle, NSI::FloatArg("width", radius*2));
		}
		else if (m_typeId == HdPrimTypeTokens->distantLight)
		{
			VtValue angle_v = sceneDelegate->GetLightParamValue(
				GetId(), UsdLuxTokens->angle);
			float angle = angle_v.Get<float>();
			nsi.SetAttribute(geo_handle, NSI::DoubleArg("angle", angle));
		}
	}

	*dirtyBits = Clean;
}

void HdNSILight::Finalize(HdRenderParam *renderParam)
{
	NSI::Context &nsi =
		static_cast<HdNSIRenderParam*>(renderParam)->AcquireSceneForEdit();
	DeleteNodes(nsi);
}

HdDirtyBits HdNSILight::GetInitialDirtyBitsMask() const
{
	return AllDirty;
}

/*
	This creates the static scene structure for a light. Only the parts which
	don't depend on attributes are done here.
*/
void HdNSILight::CreateNodes(
    HdNSIRenderParam *renderParam,
	NSI::Context &i_nsi)
{
	std::string xform_handle = GetId().GetString();
	std::string geo_handle = xform_handle + "|geo";
	std::string attr_handle = xform_handle + "|attr";
	std::string shader_handle = xform_handle + "|shader";

	i_nsi.Create(xform_handle, "transform");
	i_nsi.Connect(xform_handle, "", NSI_SCENE_ROOT, "objects");

	if (m_typeId == HdPrimTypeTokens->diskLight)
	{
		i_nsi.Create(geo_handle, "particles");
		float P[3] = { 0, 0, 0 };
		float N[3] = { 0, 0, -1 };
		i_nsi.SetAttribute(geo_handle, (
			NSI::PointsArg("P", P, 1),
			NSI::NormalsArg("N", N, 1)));
	}
	else if (m_typeId == HdPrimTypeTokens->distantLight)
	{
		i_nsi.Create(geo_handle, "environment");
	}
	i_nsi.Connect(geo_handle, "", xform_handle, "objects");

	i_nsi.Create(attr_handle, "attributes");
	i_nsi.Connect(attr_handle, "", geo_handle, "geometryattributes");

	i_nsi.Create(shader_handle, "shader");
	i_nsi.Connect(shader_handle, "", attr_handle, "surfaceshader");

	std::string shaderPath = renderParam->GetRenderDelegate()->GetDelight();
	/* FIXME: We need our own shaders. */
	if (m_typeId == HdPrimTypeTokens->diskLight)
	{
		shaderPath += "/maya/osl/areaLight";
	}
	else if (m_typeId == HdPrimTypeTokens->distantLight)
	{
		shaderPath += "/maya/osl/distantLight";
	}
	i_nsi.SetAttribute(shader_handle,
		NSI::StringArg("shaderfilename", shaderPath));

	m_nodes_created = true;
}

/*
	Delete all the nodes added to the scene for the light.
*/
void HdNSILight::DeleteNodes(
	NSI::Context &i_nsi)
{
	std::string xform_handle = GetId().GetString();
	std::string geo_handle = xform_handle + "|geo";
	std::string attr_handle = xform_handle + "|attr";
	std::string shader_handle = xform_handle + "|shader";

	i_nsi.Delete(xform_handle);
	i_nsi.Delete(geo_handle);
	i_nsi.Delete(attr_handle);
	i_nsi.Delete(shader_handle);

	m_nodes_created = false;
}

void HdNSILight::SetShaderParams(
	NSI::Context &i_nsi,
	HdSceneDelegate *sceneDelegate)
{
	std::string xform_handle = GetId().GetString();
	std::string shader_handle = xform_handle + "|shader";

	float intensity = sceneDelegate->GetLightParamValue(
		GetId(), UsdLuxTokens->intensity).Get<float>();
	float exposure = sceneDelegate->GetLightParamValue(
		GetId(), UsdLuxTokens->exposure).Get<float>();
	float diffuse = sceneDelegate->GetLightParamValue(
		GetId(), UsdLuxTokens->diffuse).Get<float>();
	float specular = sceneDelegate->GetLightParamValue(
		GetId(), UsdLuxTokens->specular).Get<float>();
	bool normalize = sceneDelegate->GetLightParamValue(
		GetId(), UsdLuxTokens->normalize).Get<bool>();
	GfVec3f color = sceneDelegate->GetLightParamValue(
		GetId(), UsdLuxTokens->color).Get<GfVec3f>();
	bool enableColorTemperature = sceneDelegate->GetLightParamValue(
		GetId(), UsdLuxTokens->enableColorTemperature).Get<bool>();

	/* Let's duplicate UsdLuxLight::ComputeBaseEmission(). Because why not.
	   Because I don't have access to USD scene to build a UsdLuxLight. */
	GfVec3f emission = color * intensity * std::exp2(exposure);
	if (enableColorTemperature)
	{
		float colorTemperature = sceneDelegate->GetLightParamValue(
			GetId(), UsdLuxTokens->colorTemperature).Get<float>();
		emission = GfCompMult(emission,
			UsdLuxBlackbodyTemperatureAsRgb(colorTemperature));
	}

	i_nsi.SetAttribute(shader_handle, (
		NSI::ColorArg("i_color", emission.data()),
		NSI::IntegerArg("normalize_area", normalize),
		NSI::FloatArg("diffuse_contribution", diffuse),
		NSI::FloatArg("specular_contribution", specular)));
}

PXR_NAMESPACE_CLOSE_SCOPE
