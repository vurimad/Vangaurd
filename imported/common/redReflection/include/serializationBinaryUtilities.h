/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "handle.h"

namespace serialization
{
	class IAsyncSource;

	namespace tools
	{
		/// Load the single (root) object from an async source without calling OnPostLoad() method
		/// this is a sync call, should be used only for editor/debug/backend operations
		/// PostLoad is not called, and imports are not loaded
		RED_REFLECTION_API THandle< ISerializable > ExtractSingleObject( IAsyncSource& source );
		RED_REFLECTION_API Bool CollectImports(IAsyncSource& source, red::DynArray<res::ResourcePath>& outImports);
		RED_REFLECTION_API Bool CollectInplaceResources(IAsyncSource& source, red::DynArray<res::ResourcePath>& outInplaceResources);

		// Extract the header and fetch GPU size from it
		RED_REFLECTION_API Uint32 GetGPUSize( IAsyncSource& source );
	}

	RED_REFLECTION_API BufferHandleRO LoadMetadataBuffer( IAsyncSource& source );

} // serialization