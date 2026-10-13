/* 
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../redReflection/include/handle.h"
#include "../../redContainers/include/dynArray.h"

class CResource;

namespace res
{
	class IBackendResourceDepot;

	struct RED_RESOURCE_API ResourceSavingOptions
	{
		IBackendResourceDepot* m_depot;	// custom resource depot for saving
		ResourcePath m_targetPath;		// custom target path (if empty, path will be extracted from the resource itself)
		Bool m_loadDeferredDataBuffers; // make sure that deferred data buffers are loaded upon saving
		Bool m_isCloner;
		Bool m_isGenerator;
		Bool m_cookedOutput;
		Bool m_magicFlagForTerrain;

		ResourceSavingOptions();
	};

	// Serialize one resource into its complete RED file image without publishing
	// it. This is the shared serialization boundary used by compound durable
	// transactions; SaveResource continues to use the same implementation.
	RED_RESOURCE_API Bool SerializeResource( const THandle< CResource >& resource, const ResourcePath& targetPath,
		const ResourceSavingOptions& options, red::DynArray< Uint8 >& outData );

	// save an existing resource object to a depot
	RED_RESOURCE_API Bool SaveResource( const THandle< CResource >& resource, const ResourceSavingOptions& options = ResourceSavingOptions() );

} // res

