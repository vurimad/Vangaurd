/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace res
{
	struct StreamingData;

	// Listens for changes in the resource streaming distances
	// needed for smooth operations in the editor (unfortunately)
	class RED_REFLECTION_API IStreamedResourceListener
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolEngine );

	public:
		virtual ~IStreamedResourceListener();
		virtual void OnStreamingDataChanged( const res::ResourcePath& path, const StreamingData& data ) = 0;
	};

} // res
