#include "light.h"

#include "renderDelegate.h"
#include "renderParam.h"
#include "rprimBase.h"

#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdLux/blackbody.h>
#include <pxr/usd/usdLux/tokens.h>

#include <cmath>

/* See USD commit b5d3809c943950cd3ff6be0467858a3297df0bb7. */
#if defined(PXR_VERSION) && PXR_VERSION <= 2011
#	define LUX_INPUT(old_token, new_token) old_token
#else
#	define LUX_INPUT(old_token, new_token) new_token
#endif

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
	std::string attr_handle = xform_handle + "|attr";

	if (!m_nodes_created)
	{
		CreateNodes(nsiRenderParam, nsi);
	}

	if (0 != (*dirtyBits & DirtyTransform))
	{
		HdNSIRprimBase::ExportTransform(
			sceneDelegate, GetId(), false, nsi, xform_handle);
	}

	if (0 != (*dirtyBits & DirtyParams))
	{
		SetShaderParams(nsi, sceneDelegate);

		if (m_typeId == HdPrimTypeTokens->diskLight ||
		    m_typeId == HdPrimTypeTokens->sphereLight)
		{
			float radius = sceneDelegate->GetLightParamValue(
				GetId(),
				UsdLuxTokens->LUX_INPUT(radius, inputsRadius)).Get<float>();
			if (radius == 0.0f)
			{
				// Set to a small value - pick this to match
				// radius used for a "point" light in 3DFM
				radius = 5e-4;

				// If it has no radius, it should be invisible to
				// the camera
				nsi.SetAttribute(attr_handle,
					NSI::IntegerArg("visibility.camera", 0));
			}
			nsi.SetAttribute(geo_handle, NSI::FloatArg("width", radius*2));
		}
		else if (m_typeId == HdPrimTypeTokens->distantLight)
		{
			VtValue angle_v = sceneDelegate->GetLightParamValue(
				GetId(), UsdLuxTokens->LUX_INPUT(angle, inputsAngle));
			float angle = angle_v.Get<float>();
			nsi.SetAttribute(geo_handle, NSI::DoubleArg("angle", angle));
		}
		else if (m_typeId == HdPrimTypeTokens->cylinderLight)
		{
			float length = sceneDelegate->GetLightParamValue(
				GetId(),
				UsdLuxTokens->LUX_INPUT(length, inputsLength)).Get<float>();
			float radius = sceneDelegate->GetLightParamValue(
				GetId(),
				UsdLuxTokens->LUX_INPUT(radius, inputsRadius)).Get<float>();
			GenCylinder(nsi, geo_handle, length, radius);
		}
		else if (m_typeId == HdPrimTypeTokens->rectLight)
		{
			float width = sceneDelegate->GetLightParamValue(
				GetId(),
				UsdLuxTokens->LUX_INPUT(width, inputsWidth)).Get<float>();
			float height = sceneDelegate->GetLightParamValue(
				GetId(),
				UsdLuxTokens->LUX_INPUT(height, inputsHeight)).Get<float>();
			float hw = 0.5f * width;
			float hh = 0.5f * height;
			float P[12] = {hw, -hh, 0, -hw, -hh, 0, -hw, hh, 0, hw, hh, 0};
			nsi.SetAttribute(geo_handle, NSI::PointsArg("P", P, 4));
		}
	}

	/* Visibility does not have a dirty bit for lights. It is part of params. */
	if (0 != (*dirtyBits & (DirtyParams | DirtyCollection)))
	{
		SyncVisibilityAndLinking(nsi, sceneDelegate);
	}

	*dirtyBits = Clean;
}

void HdNSILight::Finalize(HdRenderParam *renderParam)
{
	auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
	NSI::Context &nsi = nsiRenderParam->AcquireSceneForEdit();
	DeleteNodes(nsiRenderParam, nsi);
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

	if (m_typeId == HdPrimTypeTokens->diskLight ||
	    m_typeId == HdPrimTypeTokens->sphereLight)
	{
		i_nsi.Create(geo_handle, "particles");
		float P[3] = { 0, 0, 0 };
		float N[3] = { 0, 0, -1 };
		i_nsi.SetAttribute(geo_handle, NSI::PointsArg("P", P, 1));
		if (m_typeId == HdPrimTypeTokens->diskLight)
		{
			i_nsi.SetAttribute(geo_handle, NSI::NormalsArg("N", N, 1));
		}
	}
	else if (m_typeId == HdPrimTypeTokens->distantLight)
	{
		i_nsi.Create(geo_handle, "environment");
	}
	else if (m_typeId == HdPrimTypeTokens->domeLight)
	{
		i_nsi.Create(geo_handle, "environment");
	}
	else if (m_typeId == HdPrimTypeTokens->cylinderLight)
	{
		i_nsi.Create(geo_handle, "mesh");
		/* P depends on radius/length so is set elsewhere. */
	}
	else if (m_typeId == HdPrimTypeTokens->rectLight)
	{
		i_nsi.Create(geo_handle, "mesh");
		i_nsi.SetAttribute(geo_handle, NSI::IntegerArg("nvertices", 4));
		/* P depends on width/height so is set elsewhere. */
	}
	i_nsi.Connect(geo_handle, "", xform_handle, "objects");

	i_nsi.Create(attr_handle, "attributes");
	i_nsi.Connect(attr_handle, "", geo_handle, "geometryattributes");
	/* Make lights invisible to camera. */
	i_nsi.SetAttribute(attr_handle, NSI::IntegerArg("visibility.camera", 0));

	i_nsi.Create(shader_handle, "shader");
	i_nsi.Connect(shader_handle, "", attr_handle, "surfaceshader");

	std::string shaderPath = renderParam->GetRenderDelegate()->FindShader(
		"UsdLuxLight");
	i_nsi.SetAttribute(shader_handle,
		NSI::StringArg("shaderfilename", shaderPath));

	assert(!m_nodes_created);
	m_nodes_created = true;
	renderParam->AddLight();
}

/*
	Delete all the nodes added to the scene for the light.
*/
void HdNSILight::DeleteNodes(
	HdNSIRenderParam *renderParam,
	NSI::Context &i_nsi)
{
	if (!m_nodes_created)
		return;

	std::string xform_handle = GetId().GetString();
	std::string geo_handle = xform_handle + "|geo";
	std::string attr_handle = xform_handle + "|attr";
	std::string shader_handle = xform_handle + "|shader";

	i_nsi.Delete(xform_handle);
	i_nsi.Delete(geo_handle);
	i_nsi.Delete(attr_handle);
	i_nsi.Delete(shader_handle);
	if( !m_linking_attr_handle.empty() )
	{
		i_nsi.Delete(m_linking_attr_handle);
		m_linking_attr_handle.clear();
	}

	m_nodes_created = false;
	renderParam->RemoveLight();
}

void HdNSILight::SetShaderParams(
	NSI::Context &i_nsi,
	HdSceneDelegate *sceneDelegate)
{
	std::string xform_handle = GetId().GetString();
	std::string shader_handle = xform_handle + "|shader";

	float intensity = sceneDelegate->GetLightParamValue(
		GetId(),
		UsdLuxTokens->LUX_INPUT(intensity, inputsIntensity)).Get<float>();
	float exposure = sceneDelegate->GetLightParamValue(
		GetId(),
		UsdLuxTokens->LUX_INPUT(exposure, inputsExposure)).Get<float>();
	float diffuse = sceneDelegate->GetLightParamValue(
		GetId(), UsdLuxTokens->LUX_INPUT(diffuse, inputsDiffuse)).Get<float>();
	float specular = sceneDelegate->GetLightParamValue(
		GetId(),
		UsdLuxTokens->LUX_INPUT(specular, inputsSpecular)).Get<float>();
	bool normalize = sceneDelegate->GetLightParamValue(
		GetId(),
		UsdLuxTokens->LUX_INPUT(normalize, inputsNormalize)).Get<bool>();
	GfVec3f color = sceneDelegate->GetLightParamValue(
		GetId(), UsdLuxTokens->LUX_INPUT(color, inputsColor)).Get<GfVec3f>();
	bool enableColorTemperature = sceneDelegate->GetLightParamValue(
		GetId(), UsdLuxTokens->LUX_INPUT(
			enableColorTemperature, inputsEnableColorTemperature)).Get<bool>();

	/* Let's duplicate UsdLuxLight::ComputeBaseEmission(). Because why not.
	   Because I don't have access to USD scene to build a UsdLuxLight. */
	GfVec3f emission = color * intensity * std::exp2(exposure);
	if (enableColorTemperature)
	{
		float colorTemperature = sceneDelegate->GetLightParamValue(
			GetId(), UsdLuxTokens->LUX_INPUT(
				colorTemperature, inputsColorTemperature)).Get<float>();
		emission = GfCompMult(emission,
			UsdLuxBlackbodyTemperatureAsRgb(colorTemperature));
	}

	/* Same name remapping as HdNSIMaterial::EscapeOSLKeyword(). */
	i_nsi.SetAttribute(shader_handle, (
		NSI::ColorArg("color_", emission.data()),
		NSI::IntegerArg("normalize_", normalize),
		NSI::FloatArg("diffuse_", diffuse),
		NSI::FloatArg("specular", specular)));

	if (m_typeId == HdPrimTypeTokens->domeLight)
	{
		VtValue tex_v = sceneDelegate->GetLightParamValue(
			GetId(), UsdLuxTokens->LUX_INPUT(textureFile, inputsTextureFile));
		if (tex_v.IsHolding<SdfAssetPath>())
		{
			std::string path = tex_v.Get<SdfAssetPath>().GetResolvedPath();
			i_nsi.SetAttribute(shader_handle,
				NSI::StringArg("texturefile", path));
		}

		VtValue format_v = sceneDelegate->GetLightParamValue(
			GetId(),
			UsdLuxTokens->LUX_INPUT(textureFormat, inputsTextureFormat));
		if (format_v.IsHolding<TfToken>())
		{
			TfToken format = format_v.Get<TfToken>();
			i_nsi.SetAttribute(shader_handle,
				NSI::StringArg("textureformat", format.GetString()));
		}
	}
}

/*
	Handle the visibility attribute as well as light linking.
*/
void HdNSILight::SyncVisibilityAndLinking(
	NSI::Context &i_nsi,
	HdSceneDelegate *sceneDelegate)
{
	std::string xform_handle = GetId().GetString();
	std::string geo_handle = xform_handle + "|geo";
	std::string attr_handle = xform_handle + "|attr";

	bool visible = sceneDelegate->GetVisible(GetId());

	/*
		Check if we have light linking and create or delete the attributes node
		used by primitives as a binding point for it. Its handle is the link
		category to make this easy on the rprim side. Lights get synchronized
		before rprims which makes it ok to only create the node here.
	*/
	VtValue link = sceneDelegate->GetLightParamValue(
		GetId(), HdTokens->lightLink);
	if( link.IsHolding<TfToken>() && !link.UncheckedGet<TfToken>().IsEmpty() )
	{
		m_linking_attr_handle = link.UncheckedGet<TfToken>().GetString();
		i_nsi.Create(m_linking_attr_handle, "attributes");
	}
	else if( !m_linking_attr_handle.empty() )
	{
		i_nsi.Delete(m_linking_attr_handle);
		m_linking_attr_handle.clear();
	}

	if( !visible )
	{
		i_nsi.SetAttribute(attr_handle, NSI::IntegerArg("visibility", 0));
		/* Invisibility overrides light linking so disconnect it. Keep the
		   node and its links intact in case visibility changes later. */
		if( !m_linking_attr_handle.empty() )
		{
			i_nsi.Disconnect(
				m_linking_attr_handle, "", geo_handle, "geometryattributes");
		}
	}
	else if( !m_linking_attr_handle.empty() )
	{
		/* Make invisible and let light linking override that. */
		i_nsi.SetAttribute(attr_handle, NSI::IntegerArg("visibility", 0));
		i_nsi.Connect(
			m_linking_attr_handle, "", geo_handle, "geometryattributes");
	}
	else
	{
		i_nsi.SetAttribute(attr_handle, NSI::IntegerArg("visibility", 1));
	}
}

/*
	Generate the cylinder light geo. UsdLuxCylinderLight says:
	- The cylinder is centered at the origin and has its major axis on the X
	  axis.
	- The cylinder does not emit light from the flat end-caps.

	We don't have a native cylinder so we create one with a subdiv mesh for
	now.
*/
void HdNSILight::GenCylinder(
	NSI::Context &i_nsi,
	const std::string &i_geo,
	float i_length,
	float i_radius)
{
	const float PI = std::acos(-1);
	constexpr int lsteps = 1;
	constexpr int rsteps = 4;

	float P[rsteps][lsteps + 1][3];
	for (int i = 0; i < rsteps; ++i)
	{
		float angle = float(i) / float(rsteps) * (2.0f * PI);
		float y = i_radius * std::cos(angle);
		float z = i_radius * std::sin(angle);
		for (int j = 0; j < lsteps + 1; ++j)
		{
			float x = i_length * (-0.5f + float(j) / float(lsteps));
			P[i][j][0] = x;
			P[i][j][1] = y;
			P[i][j][2] = z;
		}
	}

	int nvertices[rsteps][lsteps];
	int indices[rsteps][lsteps][4];
	for (int i = 0; i < rsteps; ++i)
	{
		int ni = (i + 1) % rsteps;
		for (int j = 0; j < lsteps; ++j)
		{
			nvertices[i][j] = 4;
			indices[i][j][0] = ni * (lsteps + 1) + j + 0;
			indices[i][j][1] = ni * (lsteps + 1) + j + 1;
			indices[i][j][2] = i * (lsteps + 1) + j + 1;
			indices[i][j][3] = i * (lsteps + 1) + j + 0;
		}
	}

	i_nsi.SetAttribute(i_geo, (
		NSI::StringArg("subdivision.scheme", "catmull-clark"),
		NSI::PointsArg("P", &P[0][0][0], rsteps * (lsteps + 1)),
		NSI::IntegersArg("nvertices", &nvertices[0][0], rsteps * lsteps),
		NSI::IntegersArg("P.indices", &indices[0][0][0], rsteps * lsteps * 4)));
}

PXR_NAMESPACE_CLOSE_SCOPE
