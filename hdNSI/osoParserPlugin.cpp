#include "osoParserPlugin.h"

#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/ndr/node.h>
#include <pxr/usd/ndr/nodeDiscoveryResult.h>

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(HdNSIOsoParserPlugin)

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    ((discoveryType, "oso"))
    ((sourceType, "OSL"))
);

NdrNodeUniquePtr HdNSIOsoParserPlugin::Parse(
    const NdrNodeDiscoveryResult& discoveryResult)
{
    return NdrNodeUniquePtr{new NdrNode(
        discoveryResult.identifier,
        discoveryResult.version,
        discoveryResult.name,
        discoveryResult.family,
        _tokens->sourceType, /* Should probably be surface/displacement/etc */
        _tokens->sourceType,
        discoveryResult.uri,
#if !defined(PXR_VERSION) || PXR_VERSION > 1911
        discoveryResult.resolvedUri,
#endif
        {})};
}

const NdrTokenVec& HdNSIOsoParserPlugin::GetDiscoveryTypes() const
{
    static const NdrTokenVec discovery_types{_tokens->discoveryType};
    return discovery_types;
}

const TfToken& HdNSIOsoParserPlugin::GetSourceType() const
{
    return _tokens->sourceType;
}

PXR_NAMESPACE_CLOSE_SCOPE
// vim: set softtabstop=4 expandtab shiftwidth=4:
