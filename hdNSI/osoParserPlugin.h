#ifndef HDNSI_OSO_PARSER_PLUGIN_H
#define HDNSI_OSO_PARSER_PLUGIN_H

#include <pxr/pxr.h>
#include <pxr/usd/ndr/parserPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

/**
    \brief HdNSI parser plugin for shaders.

    This does the minimal amount of work so Hydra will let us have our shaders.
*/
class HdNSIOsoParserPlugin final : public NdrParserPlugin
{
public:
    NdrNodeUniquePtr Parse(
        const NdrNodeDiscoveryResult& discoveryResult) override;

    const NdrTokenVec& GetDiscoveryTypes() const override;

    const TfToken& GetSourceType() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=4 expandtab shiftwidth=4:
