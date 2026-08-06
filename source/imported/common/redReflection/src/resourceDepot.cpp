/*
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */
#include "build.h"
#include "resourceDepot.h"

static res::IResourceDepot* GResourceDepot = nullptr;

res::IResourceDepot* GetResourceDepot()
{
	return GResourceDepot;
}

void SetResourceDepot( res::IResourceDepot* resourceDepot )
{
	GResourceDepot = resourceDepot;
}

namespace res
{
IResourceDepot::~IResourceDepot() = default;

red::SharedPtr<serialization::IAsyncSource> IResourceDepot::CreateResourceAsyncSource( const ResourcePath resourcePath )
{
	return CreateResourceAsyncSource( resourcePath, res::CreateResourceAsyncSourceParams() );
}

// Default implementation
void IResourceDepot::ResolveResourcePathBulk( const red::ArraySpan< res::ResourcePath >& inOutResourcePaths ) const
{
	for ( Uint32 i : inOutResourcePaths.Indices() )
	{
		inOutResourcePaths[i] = ResolveResourcePath( inOutResourcePaths[i] );
	}
}

} // res
