#include "pxr/imaging/hdNSI/material.h"
#include "pxr/imaging/hdNSI/renderDelegate.h"
#include "pxr/imaging/hdNSI/renderParam.h"
#include "pxr/usd/sdf/assetPath.h"

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
		if (e.first != HdMaterialTerminalTokens->surface)
			continue;

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

		if (e.first == HdMaterialTerminalTokens->surface)
		{
			/*
				Assume the last node is the head of the network. This should
				always be true from the way the network is parsed in
				UsdImagingMaterialAdapter. I could not find a way to get the
				actual value of the material's "outputs:surface", etc through
				the Hydra API.
			*/
			nsi.Connect(
				e.second.nodes.back().path.GetString(), "",
				mat_handle, "surfaceshader");

		}
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
		const std::string name = EscapeOSLKeyword(p.first.GetString());
		const VtValue &v = p.second;
		if (v.IsHolding<TfToken>())
		{
			args.Add(new NSI::StringArg(name, v.Get<TfToken>().GetString()));
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
*/
std::string HdNSIMaterial::EscapeOSLKeyword(const std::string &name)
{
	if (name == "color" || name == "normal")
		return name + "_";
	return name;
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
