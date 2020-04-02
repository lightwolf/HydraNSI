#include "pxr/imaging/hdNSI/material.h"
#include "pxr/imaging/hdNSI/renderDelegate.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/usd/sdf/assetPath.h"

#include <cstring>

PXR_NAMESPACE_OPEN_SCOPE


HdNSIMaterial::HdNSIMaterial(
	const SdfPath &sprimId)
:
	HdMaterial{sprimId},
	m_attributes_created{false}
{
}

void HdNSIMaterial::Reload()
{
	/* Nothing to do for us. */
}

void HdNSIMaterial::Sync(
	HdSceneDelegate *sceneDelegate,
	HdRenderParam *renderParam,
	HdDirtyBits *dirtyBits)
{
	auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
	NSI::Context &nsi = nsiRenderParam->AcquireSceneForEdit();

	std::string mat_handle = GetId().GetString() + "|mat";

	if (!m_attributes_created)
	{
		nsi.Create(mat_handle, "attributes");
		m_attributes_created = true;
	}

	if (0 != (*dirtyBits & DirtyResource))
	{
		VtValue v = sceneDelegate->GetMaterialResource(GetId());
		if (!v.IsEmpty())
		{
			ExportNetworks(
				nsi, nsiRenderParam,
				v.Get<HdMaterialNetworkMap>());
		}
	}

	*dirtyBits = Clean;
}

void HdNSIMaterial::Finalize(HdRenderParam *renderParam)
{
	auto nsiRenderParam = static_cast<HdNSIRenderParam*>(renderParam);
	NSI::Context &nsi = nsiRenderParam->AcquireSceneForEdit();

	std::string mat_handle = GetId().GetString() + "|mat";

	if (m_attributes_created)
	{
		nsi.Delete(mat_handle);
		m_attributes_created = false;
	}
	DeleteShaderNodes(nsi);
}

HdDirtyBits HdNSIMaterial::GetInitialDirtyBitsMask() const
{
	return AllDirty;
}

void HdNSIMaterial::ExportNetworks(
	NSI::Context &nsi,
	HdNSIRenderParam *renderParam,
	const HdMaterialNetworkMap &networks)
{
	/*
		First, delete the old networks. This should eventually be improved to
		only clear/update the network which actually changes. But there's no
		point in doing that until we also support displacement.
	*/
	DeleteShaderNodes(nsi);

	std::string mat_handle = GetId().GetString() + "|mat";

	for (auto &e : networks.map)
	{
		if (e.second.nodes.empty())
			continue;

		const char *nsi_terminal = "";
		if (e.first == HdMaterialTerminalTokens->surface)
		{
			nsi_terminal = "surfaceshader";
		}
		else if (e.first == HdMaterialTerminalTokens->displacement)
		{
			nsi_terminal = "displacementshader";
		}
		else if (e.first == HdMaterialTerminalTokens->volume)
		{
			nsi_terminal = "volumeshader";
		}
		else
		{
			continue; /* unsupported */
		}

		for (const HdMaterialNode &node : e.second.nodes)
		{
			ExportNode(nsi, renderParam, node);
		}

		for (const HdMaterialRelationship &r : e.second.relationships )
		{
			nsi.Connect(
				r.inputId.GetString(),
				EscapeOSLKeyword(r.inputName.GetString()),
				r.outputId.GetString(),
				EscapeOSLKeyword(r.outputName.GetString()));
		}

		/*
			Assume the last node is the head of the network. This should always
			be true from the way the network is parsed in
			UsdImagingMaterialAdapter. I could not find a way to get the actual
			value of the material's "outputs:surface", etc through Hydra.
		*/
		nsi.Connect(
			e.second.nodes.back().path.GetString(), "",
			mat_handle, nsi_terminal);
	}
}

namespace
{
/* Changes "<UDIM>" to "UDIM" in the path so both will be recognized. */
std::string FixUDIM(const std::string &path)
{
	auto p = path.rfind("<UDIM>");
	if (p == std::string::npos)
		return path;
	std::string newpath(path);
	newpath.replace(p, 6, "UDIM");
	return newpath;
}

/*
	Returns true if the given parameter name ends with some specific suffix and
	the node also has a parameter with the same root and a second suffix.
*/
bool HasRampParam(
	const HdMaterialNode &node,
	const std::string &basename,
	const char *old_suffix,
	const char *new_suffix)
{
	size_t l = strlen(old_suffix);
	if (basename.size() <= l)
		return false;

	if (0 != std::strcmp(basename.c_str() + basename.size() - l, old_suffix))
		return false;

	std::string newname = basename.substr(0, basename.size() - l);
	newname += new_suffix;

	/*
		I'm not entirely certain about TfToken creation performance so we'll
		just directly compare to all parameter names for now. This is not great
		but it is a known amount of "not great" I can live with.
	*/
	for (auto &p : node.parameters)
	{
		if (p.first == newname)
			return true;
	}
	return false;
}

/*
	Special handling for ramp parameters coming from Houdini. This can either
	rename them, skip them, or output them in here differently. For a color
	ramp with a base name of 'foo', the Houdini default shader translator
	provides us with:

	int foo -> number of control points
	string foo_basis -> interpolation type
	float[n] foo_keys -> the control point locations
	color[n] foo_values -> the control point values

	We must remap this to what our shaders usually expect:
	int[] foo_Interp -> interpolation type (can be a single value)
	float[n] foo_Position -> same as foo_keys above
	color[n] foo_ColorValue -> same as foo_values above

	In an ideal world, this would be driven by shader metadata. For now, I'm
	just checking parameter names.

	FIXME: This does not cover connections.

	This returns true if the regular output should be skipped.
*/
bool RampFixup(
	std::string &name,
	const VtValue &v,
	NSI::ArgumentList &args,
	const HdMaterialNode &node)
{
	if (v.IsHolding<int>() && HasRampParam(node, name, "", "_keys"))
	{
		/* Skip this one. We get the count from the array lengths. */
		return true;
	}

	if (HasRampParam(node, name, "_basis", "_keys"))
	{
		/* Remap to an integer and output it here. */
		int interp = 4; /* hopefully smoothstep */
		std::string mode = v.Get<std::string>();
		if (mode == "constant")
			interp = 0;
		else if (mode == "linear")
			interp = 1;
		/* FIXME: Support more modes. Fix the shaders. */

		name = name.substr(0, name.size() - 6) + "_Interp";
		args.Add(NSI::Argument::New(name)
			->SetArrayType(NSITypeInteger, 1)
			->CopyValue(&interp, sizeof(interp)));
		return true;
	}

	if (HasRampParam(node, name, "_keys", "_basis"))
	{
		/* Rename it. Let standard output handle the value. */
		name = name.substr(0, name.size() - 5) + "_Position";
		return false;
	}

	if (HasRampParam(node, name, "_values", "_basis"))
	{
		/* Rename it. Let standard output handle the value. */
		name = name.substr(0, name.size() - 7) + "_ColorValue";
		return false;
	}

	return false;
}
}

void HdNSIMaterial::ExportNode(
	NSI::Context &nsi,
	HdNSIRenderParam *renderParam,
	const HdMaterialNode &node)
{
	std::string node_handle = node.path.GetString();
	std::string shader = renderParam->GetRenderDelegate()
		->FindShader(node.identifier);

	nsi.Create(node_handle, "shader");
	/* Record this new node. For deletion later. */
	m_network_nodes.push_back(node_handle);

	NSI::ArgumentList args;
	args.Add(new NSI::StringArg("shaderfilename", shader));
	for (auto &p : node.parameters)
	{
		std::string name = p.first.GetString();
		const VtValue &v = p.second;

		if (RampFixup(name, v, args, node))
			continue;

		name = EscapeOSLKeyword(name);

		if (v.IsHolding<TfToken>())
		{
			args.Add(new NSI::StringArg(name, v.Get<TfToken>().GetString()));
		}
		else if (v.IsHolding<std::string>())
		{
			args.Add(new NSI::StringArg(name, v.Get<std::string>()));
		}
		else if (v.IsHolding<float>())
		{
			args.Add(new NSI::FloatArg(name, v.Get<float>()));
		}
		else if (v.IsHolding<GfVec3f>())
		{
			args.Add(new NSI::ColorArg(name, v.Get<GfVec3f>().data()));
		}
		else if (v.IsHolding<GfVec4f>())
		{
			args.Add(
				NSI::Argument::New(name)
				->SetArrayType(NSITypeFloat, 4)
				->CopyValue(v.Get<GfVec4f>().data(), sizeof(float)*4));
		}
		else if (v.IsHolding<int>())
		{
			args.Add(new NSI::IntegerArg(name, v.Get<int>()));
		}
		else if (v.IsHolding<SdfAssetPath>())
		{
			std::string path = v.Get<SdfAssetPath>().GetResolvedPath();
			path = FixUDIM(path);
			args.Add(new NSI::StringArg(name, path));
			/* Assume the asset is a texture for now. */
			args.Add(new NSI::StringArg(name + ".meta.colorspace", "auto"));
		}
		else if (v.IsHolding<VtArray<float>>())
		{
			const auto &v_array = v.Get<VtArray<float>>();
			args.Add(NSI::Argument::New(name)
				->SetArrayType(NSITypeFloat, v_array.size())
				->SetValuePointer(v_array.cdata()));
		}
		else if (v.IsHolding<VtArray<GfVec3f>>())
		{
			const auto &v_array = v.Get<VtArray<GfVec3f>>();
			args.Add(NSI::Argument::New(name)
				->SetArrayType(NSITypeColor, v_array.size())
				->SetValuePointer(v_array.cdata()->data()));
		}
	}
	nsi.SetAttribute(node_handle, args);

}

/*
	Delete all the shader nodes exported for this material.
*/
void HdNSIMaterial::DeleteShaderNodes(NSI::Context &nsi)
{
	for (std::string &handle : m_network_nodes)
	{
		nsi.Delete(handle);
	}
	m_network_nodes.clear();
}

/*
	Alter the name of parameters which are reserved OSL keywords to something
	which can actually be declared in the shader.

	For example, UsdPreviewSurface's 'normal' attribute gets exported to the
	'normal_' shader paramter. 

	We also include 'normalize' as overwriting that function with a parameter
	makes for really painful shader writing.
*/
std::string HdNSIMaterial::EscapeOSLKeyword(const std::string &name)
{
	if (name == "color" || name == "normal" | name == "normalize")
		return name + "_";
	return name;
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
