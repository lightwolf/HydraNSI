#ifndef HDNSI_DISCOVERY_PLUGIN_H
#define HDNSI_DISCOVERY_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/usd/ndr/discoveryPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/**
    \brief Discovery plugin for HdNSI.

    For now, this only needs to find the supported shaders.
*/
class HdNSIDiscoveryPlugin final : public NdrDiscoveryPlugin
{
public:
    HdNSIDiscoveryPlugin();

    virtual NdrNodeDiscoveryResultVec DiscoverNodes(const Context&) override;
    virtual const NdrStringVec& GetSearchURIs() const override;

private:
    NdrStringVec m_search_paths;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=4 expandtab shiftwidth=4:
