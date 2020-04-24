#include "discoveryPlugin.h"

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/thisPlugin.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/ndr/filesystemDiscoveryHelpers.h>

#include <delight.h>
#include <nsi_dynamic.hpp>

/* Useful traces if shaders are not being found. */
//#define DISCOVERY_TRACES

#ifdef DISCOVERY_TRACES
#   include <iostream>
#endif

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_DISCOVERY_PLUGIN(HdNSIDiscoveryPlugin)

HdNSIDiscoveryPlugin::HdNSIDiscoveryPlugin()
{
    /* Get our own shaders. */
    PlugPluginPtr plugin = PLUG_THIS_PLUGIN;
    m_search_paths.push_back(PlugFindPluginResource(plugin, "osl", false));

    /* Also get the ones shipped with the renderer. */
    NSI::DynamicAPI capi;
    decltype(&DlGetInstallRoot) PDlGetInstallRoot;
    capi.LoadFunction(PDlGetInstallRoot, "DlGetInstallRoot");
    if (PDlGetInstallRoot)
    {
        std::string delight = PDlGetInstallRoot();
        m_search_paths.push_back(TfStringCatPaths(delight, "osl"));
    }
#ifdef DISCOVERY_TRACES
    for( const auto &p : m_search_paths )
    {
        std::cerr << "HdNSIDiscovery: searching in: " << p << "\n";
    }
#endif
}

NdrNodeDiscoveryResultVec HdNSIDiscoveryPlugin::DiscoverNodes(
    const Context &context)
{
    NdrNodeDiscoveryResultVec result = NdrFsHelpersDiscoverNodes(
        GetSearchURIs(), {"oso"}, true, &context);

#ifdef DISCOVERY_TRACES
    for( const NdrNodeDiscoveryResult &r : result )
    {
        std::cerr << "HdNSIDiscovery: found " << r.identifier.GetString()
            << " as " << r.uri << "\n";
    }
#endif

    return result;
}

const NdrStringVec& HdNSIDiscoveryPlugin::GetSearchURIs() const
{
    return m_search_paths;
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
