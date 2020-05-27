#include "material.h"
#include "renderDelegate.h"
#include "renderParam.h"

#include <pxr/usd/sdf/assetPath.h>

#include <cstring>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
	_tokens,
	(vdbVolume)
	/* Special volume parameters. */
	((densitygrid, "densitygrid"))
	((colorgrid, "colorgrid"))
	((temperaturegrid, "temperaturegrid"))
	((emissionintensitygrid, "emissionintensitygrid"))
	((velocitygrid, "velocitygrid"))
	((velocityscale, "velocityscale"))
);

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

std::weak_ptr<HdNSIMaterial::VolumeCallbacks>
HdNSIMaterial::GetVolumeCallbacks()
{
	std::lock_guard<std::mutex> guard{m_volume_callbacks_mutex};
	if (!m_volume_callbacks)
	{
		m_volume_callbacks.reset(new VolumeCallbacks);
	}
	return m_volume_callbacks;
}

/**
	\returns
		The list of special vdbVolume parameters which should actually be
		volume node parameter.
*/
const std::array<TfToken, 6>& HdNSIMaterial::VolumeNodeParameters()
{
	static std::array<TfToken, 6> p
	{
		_tokens->densitygrid,
		_tokens->colorgrid,
		_tokens->temperaturegrid,
		_tokens->emissionintensitygrid,
		_tokens->velocitygrid,
		_tokens->velocityscale
	};
	return p;
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
			m_vdbVolume.reset();
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

		if (e.first == HdMaterialTerminalTokens->volume && m_volume_callbacks)
		{
			std::lock_guard<std::mutex> guard{m_volume_callbacks->m_mutex};
			for( VolumeCB *cb : *m_volume_callbacks )
			{
				cb->NewVDBNode(nsi, this);
			}
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

void IsRamp(
	const DlShaderInfo::Parameter &param,
	bool *ramp,
	bool *related_to_ramp)
{
	for( const auto &m : param.metadata )
	{
		if (m.name == "widget" &&
		    m.sdefault.size() >= 1 &&
		    m.sdefault[0].size() > 4 &&
		    0 == std::strcmp(m.sdefault[0].end() - 4, "Ramp"))
		{
			*ramp = true;
		}
		if (m.name == "related_to_widget" &&
		    m.sdefault.size() >= 1 &&
		    m.sdefault[0].size() > 4 &&
		    0 == std::strcmp(m.sdefault[0].end() - 4, "Ramp"))
		{
			*related_to_ramp = true;
		}
	}
}

void FixRamps(
	const DlShaderInfo &shader_meta,
	HdMaterialNode &node)
{
	for( const auto &param : shader_meta.params() )
	{
		bool ramp = false, related_to_ramp = false;
		IsRamp(param, &ramp, &related_to_ramp);
		//printf("isramp: %d %d\n", ramp, related_to_ramp);
		if (ramp)
		{
			/*
				Find the base name and remove any parameter with that name. It
				should be the count of key/values, which we don't need.
			*/
			std::string base_name = param.name.string();
			auto pos = base_name.rfind('_');
			if (pos != std::string::npos)
			{
				base_name = base_name.substr(0, pos);
				node.parameters.erase(TfToken(base_name));
			}
		}
		if (related_to_ramp)
		{
			/*
				Secondary ramp parameter. If its type is int and we're given a
				string, this is the interpolation parameter and it requires
				conversion.
			*/
			auto it = node.parameters.find(TfToken(param.name.string()));
			if (param.type.elementtype == NSITypeInteger &&
			    it != node.parameters.end() &&
			    it->second.IsHolding<std::string>())
			{
				std::string interp = it->second.Get<std::string>();
				if (interp == "constant")
				{
					it->second = VtArray<int>({0});
				}
				else if (interp == "linear")
				{
					it->second = VtArray<int>({1});
				}
				else
				{
					/* catmull-rom spline for everything else, for now. */
					it->second = VtArray<int>({3});
				}
			}
		}
	}
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
	/* Copy of the node on which we'll apply some fixes. */
	HdMaterialNode exported_node = node;

	if (node.identifier == _tokens->vdbVolume)
	{
		/* Grab a copy of that node, for use by HdNSIVolume. */
		m_vdbVolume.reset(new HdMaterialNode{node});
		/* Remove the parameters which are moved to the volume. */
		for( const TfToken &volume_p : VolumeNodeParameters() )
		{
			exported_node.parameters.erase(volume_p);
		}
	}

	/* Load metadata and apply ramp fixes. */
	DlShaderInfo *si = renderParam->GetRenderDelegate()->GetShaderInfo(shader);
	if (si)
	{
		FixRamps(*si, exported_node);
	}

	nsi.Create(node_handle, "shader");
	/* Record this new node. For deletion later. */
	m_network_nodes.push_back(node_handle);

	NSI::ArgumentList args;
	args.Add(new NSI::StringArg("shaderfilename", shader));
	for (auto &p : exported_node.parameters)
	{
		std::string name = p.first.GetString();
		const VtValue &v = p.second;

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
		else if (v.IsHolding<VtArray<int>>())
		{
			const auto &v_array = v.Get<VtArray<int>>();
			args.Add(NSI::Argument::New(name)
				->SetArrayType(NSITypeInteger, v_array.size())
				->SetValuePointer(v_array.cdata()));
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

	Similar for 'diffuse' which is a closure name and causes warnings.
*/
std::string HdNSIMaterial::EscapeOSLKeyword(const std::string &name)
{
	if (name == "color" || name == "normal" ||
	    name == "normalize" || name == "diffuse")
	{
		return name + "_";
	}
	return name;
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
