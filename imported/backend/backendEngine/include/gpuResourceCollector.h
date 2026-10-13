/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redSystem/include/redThreadsThread.h"
#include "../../../common/redContainers/include/hashMap.h"
#include "../../../common/redContainers/include/hashSet.h"
#include "../../../common/redContainers/include/dynArray.h"

namespace tools
{

	class BACKEND_ENGINE_API GpuResourceCollector final : public red::Thread
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

	public:
		GpuResourceCollector();

		Bool HasNewData( Uint64 editor ) const;

		void Start();
		void Stop();

		void RegisterEditor( Uint64 editor );
		void UnregisterEditor( Uint64 editor );

		void SetPartiallyCollectedResources( Uint64 editor, red::DynArray< res::ResourcePath >&& collectedResources );
		Bool AcquireAllResourcePaths( Uint64 editor, red::DynArray< res::ResourcePath >& textures, red::DynArray< res::ResourcePath >& meshes, red::DynArray< res::ResourcePath >& materials, red::DynArray< res::ResourcePath >& videos);

	private:
		struct Resources
		{
			struct ResourceType
			{
				red::DynArray< red::String > m_extensions;
				red::DynArray< res::ResourcePath > m_resources;
			};

			Resources();

			ResourceType m_textures;
			ResourceType m_meshes;
			ResourceType m_materials;
			ResourceType m_videos;
			Bool m_hasNewData = false;
		};

		virtual void ThreadFunc() override;

		void CollectResources();
		const red::Set< res::ResourcePath > CollectResources( const res::ResourcePath& resourcePath );
		void FilterGpuResource( const res::ResourcePath& path, Resources& resources);

		red::HashMap< const res::ResourcePath, red::Set< res::ResourcePath > > m_recursiveDepsCache;
		red::HashSet< res::ResourcePath > m_recursiveVisitedDeps;

		mutable red::RWSpinLock m_allEditorResourcesLock;
		red::HashMap< Uint64, Resources > m_allEditorResources;

		mutable red::RWSpinLock m_editorPartiallyCollectedResourcesLock;
		red::HashMap< Uint64, red::DynArray< res::ResourcePath > > m_editorPartiallyCollectedResources;

		red::Atomic< Bool > m_closeRequested;
	};

}

extern BACKEND_ENGINE_API tools::GpuResourceCollector* GGpuResourceCollector;