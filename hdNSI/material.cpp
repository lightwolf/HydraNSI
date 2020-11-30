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

#if defined(PXR_VERSION) && PXR_VERSION <= 2008
void HdNSIMaterial::Reload()
{
	/* Nothing to do for us. */
}
#endif

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
		UseDefaultShader(nsi, nsiRenderParam, mat_handle, v.IsEmpty());
		if (!v.IsEmpty())
		{
			ExportNetworks(
				nsi, nsiRenderParam,
				v.Get<HdMaterialNetworkMap>());
		}
	}

	*dirtyBits = Clean;
}

struct HdNSIMaterial::DefaultConnectionList
{
	struct DefaultConnection
	{
		std::string m_from_handle;
		std::string m_from_attribute;
		std::string m_to_handle;
		std::string m_to_attribute;
	};

	std::vector<DefaultConnection> m_connections;

	void AddConnection(
		HdNSIRenderDelegate *renderDelegate,
		const std::string &from_type,
		const std::string &to_handle,
		const DlShaderInfo::Parameter &to_param)
	{
		DefaultConnection c;
		DlShaderInfo *shader = renderDelegate->GetDefaultShader(
			from_type, &c.m_from_handle);
		if( !shader )
			return;
		/* Find first output parameter with matching type. */
		for( const auto &param : shader->params() )
		{
			if( param.isoutput && param.type == to_param.type )
			{
				c.m_from_attribute = param.name.string();
				c.m_to_handle = to_handle;
				c.m_to_attribute = to_param.name.string();
				m_connections.emplace_back(c);
				return;
			}
		}
	}

	void RemoveConnection(
		const std::string to_handle,
		const std::string to_attribute)
	{
		for( unsigned i = 0; i < m_connections.size(); ++i )
		{
			if( m_connections[i].m_to_handle == to_handle &&
			    m_connections[i].m_to_attribute == to_attribute )
			{
				std::swap(m_connections[i], m_connections.back());
				m_connections.pop_back();
				break;
			}
		}
	}

	void Export(NSI::Context &nsi)
	{
		for( const auto &c : m_connections )
		{
			nsi.Connect(
				c.m_from_handle, c.m_from_attribute,
				c.m_to_handle, c.m_to_attribute);
		}
	}
};

void HdNSIMaterial::UseDefaultShader(
	NSI::Context &nsi,
	HdNSIRenderParam *renderParam,
	const std::string &mat_handle,
	bool use_default)
{
	if (use_default == m_use_default_shader)
		return;

	if (use_default)
	{
		/* Delete anything we exported previously. */
		DeleteShaderNodes(nsi);
		/*
			Connect the default material network. This case (an empty material
			resource) is what happens when materials are disabled globally by
			Hydra. ie. usdview's View/Enable Scene Materials.
		*/
		nsi.Connect(
			renderParam->GetRenderDelegate()->DefaultSurfaceNode(), "",
			mat_handle, "surfaceshader");
		m_use_default_shader = true;
	}
	else
	{
		/* Disconnect previously connected default shader. */
		nsi.Disconnect(
			renderParam->GetRenderDelegate()->DefaultSurfaceNode(), "",
			mat_handle, "surfaceshader");
	}
	m_use_default_shader = use_default;
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
	std::string mat_handle = GetId().GetString() + "|mat";
	DefaultConnectionList default_connections;

	for (auto &e : networks.map)
	{
		/*
			We check against the previously exported network and do nothing if
			it has not actually changed. This could happen if eg. a surface is
			updated but the displacement is not.
		*/
		const char *nsi_terminal = "";
		if (e.first == HdMaterialTerminalTokens->surface)
		{
			if (m_surface_network == e.second)
				continue;
			DeleteOneNetwork(nsi, m_surface_network, e.second);
			nsi_terminal = "surfaceshader";
		}
		else if (e.first == HdMaterialTerminalTokens->displacement)
		{
			if (m_displacement_network == e.second)
				continue;
			DeleteOneNetwork(nsi, m_displacement_network, e.second);
			nsi_terminal = "displacementshader";
		}
		else if (e.first == HdMaterialTerminalTokens->volume)
		{
			if (m_volume_network == e.second)
				continue;
			DeleteOneNetwork(nsi, m_volume_network, e.second);
			nsi_terminal = "volumeshader";
			m_vdbVolume.reset();
		}
		else
		{
			continue; /* unsupported */
		}

		if (e.second.nodes.empty())
			continue;

		for (const HdMaterialNode &node : e.second.nodes)
		{
			ExportNode(nsi, renderParam, node, default_connections);
		}

		for (const HdMaterialRelationship &r : e.second.relationships )
		{
			const std::string &to_handle = r.outputId.GetString();
			const std::string &to_attribute =
				EscapeOSLKeyword(r.outputName.GetString());
			/* Remove any default connection we might be replacing. */
			default_connections.RemoveConnection(to_handle, to_attribute);
			/* Connect. */
			nsi.Connect(
				r.inputId.GetString(),
				EscapeOSLKeyword(r.inputName.GetString()),
				to_handle,
				to_attribute);
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

	default_connections.Export(nsi);

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
	const HdMaterialNode &node,
	DefaultConnectionList &default_connections)
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

	/* We can't do anything useful without a shader. */
	if( shader.empty() )
		return;

	/* Load metadata and apply ramp fixes. */
	DlShaderInfo *si = renderParam->GetRenderDelegate()->GetShaderInfo(shader);
	if (si)
	{
		FixRamps(*si, exported_node);

		/* Record any default connections that might need to be made. */
		for( const auto &param : si->params() )
		{
			for( const auto &meta : param.metadata )
			{
				if( meta.name == "default_connection" &&
				    meta.type.IsOneString() )
				{
					default_connections.AddConnection(
						renderParam->GetRenderDelegate(),
						meta.sdefault[0].string(), /* source type */
						node_handle,
						param);
				}
			}
		}
	}

	nsi.Create(node_handle, "shader");

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
	DeleteOneNetwork(nsi, m_surface_network, {});
	DeleteOneNetwork(nsi, m_displacement_network, {});
	DeleteOneNetwork(nsi, m_volume_network, {});
}

/*
	Delete the shader nodes for one shading network and copy a new network over
	it.
*/
void HdNSIMaterial::DeleteOneNetwork(
	NSI::Context &nsi,
	HdMaterialNetwork &network,
	const HdMaterialNetwork &new_network)
{
	for (const HdMaterialNode &node : network.nodes)
	{
		nsi.Delete(node.path.GetString());
	}
	network = new_network;
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
