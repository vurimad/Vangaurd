#include "build.h"
#include "gpuResourceCollector.h"
#include "../../../common/redReflection/include/resourceCollector.h"
#include "../../../common/redReflection/include/resource.h"

BACKEND_ENGINE_API tools::GpuResourceCollector* GGpuResourceCollector = nullptr;

namespace tools
{

	GpuResourceCollector::GpuResourceCollector()
		: red::Thread( "GpuResourceCollector" )
		, m_closeRequested( false )
	{
	}

	void GpuResourceCollector::Start()
	{
		InitThread();
	}

	void GpuResourceCollector::Stop()
	{
		m_closeRequested.SetValue( true );
		JoinThread();
	}

	void GpuResourceCollector::RegisterEditor( Uint64 editor )
	{
		RED_SCOPE_LOCK( m_editorPartiallyCollectedResourcesLock );
		m_editorPartiallyCollectedResources[editor] = {};
	}

	void GpuResourceCollector::UnregisterEditor( Uint64 editor )
	{
		RED_SCOPE_LOCK( m_editorPartiallyCollectedResourcesLock );
		m_editorPartiallyCollectedResources.Remove( editor );
	}

	Bool GpuResourceCollector::HasNewData( Uint64 editor ) const
	{
		RED_SCOPE_SHARED_LOCK( m_allEditorResourcesLock );
		auto it = m_allEditorResources.Find( editor );
		return it != m_allEditorResources.End() && it.Value().m_hasNewData;
	}

	void GpuResourceCollector::SetPartiallyCollectedResources( Uint64 editor, red::DynArray< res::ResourcePath >&& collectedResources )
	{
		RED_SCOPE_LOCK( m_editorPartiallyCollectedResourcesLock );
		m_editorPartiallyCollectedResources[ editor ] = std::move( collectedResources );
	}

	Bool GpuResourceCollector::AcquireAllResourcePaths( Uint64 editor, red::DynArray< res::ResourcePath >& textures, red::DynArray< res::ResourcePath >& meshes, red::DynArray< res::ResourcePath >& materials, red::DynArray< res::ResourcePath >& videos)
	{
		RED_SCOPE_LOCK( m_allEditorResourcesLock );
		auto it = m_allEditorResources.Find( editor );
		if ( it != m_allEditorResources.End() )
		{
			textures.Swap( it.Value().m_textures.m_resources );
			meshes.Swap( it.Value().m_meshes.m_resources );
			materials.Swap( it.Value().m_materials.m_resources );
			videos.Swap(it.Value().m_videos.m_resources);
			it.Value().m_hasNewData = false;
			return true;
		}

		return false;
	}

	void GpuResourceCollector::ThreadFunc()
	{
		// needs to register ourself in Slab due to fact that job uses Slab directly (otherwise we might end up with OOM)
		red::memory::RegisterCurrentThread( "GpuResourceCollector" );

		red::Timer timer;
		while ( !m_closeRequested.GetValue() )
		{
			const auto diff = timer.GetSeconds();
			if ( diff >= 1.0 )
			{
				CollectResources();
				timer.Reset();
			}
			else
			{
				red::SleepOnCurrentThread( 500 );
			}
		}
	}

	void GpuResourceCollector::CollectResources()
	{
		red::HashMap< Uint64, red::DynArray< res::ResourcePath > > editorPartiallyCollectedResources{ red::PoolDebug() };
		{
			RED_SCOPE_LOCK( m_editorPartiallyCollectedResourcesLock );
			editorPartiallyCollectedResources.Swap( m_editorPartiallyCollectedResources );
		}

		for ( const auto& partiallyCollectedResources : editorPartiallyCollectedResources )
		{
			red::Set< res::ResourcePath > paths{ red::PoolDebug() };
			for ( const res::ResourcePath& path : partiallyCollectedResources.Value() )
			{
				paths.Insert( path );
				paths.Union( CollectResources( path ) );
			}

			{
				RED_SCOPE_LOCK( m_allEditorResourcesLock );
				auto& obj = m_allEditorResources[ partiallyCollectedResources.Key() ];

				obj.m_textures.m_resources.Clear();
				obj.m_meshes.m_resources.Clear();
				obj.m_materials.m_resources.Clear();
				for ( const auto& path : paths )
				{
					FilterGpuResource( path, obj );
				}

				obj.m_hasNewData = true;
			}
		}
	}

	const red::Set< res::ResourcePath > GpuResourceCollector::CollectResources( const res::ResourcePath& resourcePath )
	{
		if ( !m_recursiveDepsCache.KeyExist( resourcePath ) )
		{
			red::Set< res::ResourcePath > result;

			auto addResult = m_recursiveVisitedDeps.Insert( resourcePath );
			if ( addResult.IsSuccessful() )
			{
				auto resource = GResourceLoader->TryAcquiringLoadedResource( resourcePath );
				if ( !resource )
				{
					return m_recursiveDepsCache[resourcePath];
				}

				ResourceCollector resourceCollector;
				resourceCollector.Run( resource );

				red::DynArray< res::ResourcePath > resourceDependencies;
				resourceCollector.GetCollectedPaths( resourceDependencies );

				for ( const auto& depPath : resourceDependencies )
				{
					result.InsertUnsorted( depPath );
				}
				result.MakeClean();

				for ( const auto& depPath : resourceDependencies )
				{
					result.Union( CollectResources( depPath ) );
				}
			}
			else
			{
				RED_LOG_ERROR( "Recursive dependecy found in: %s", resourcePath.ToDebugString() );
			}

			m_recursiveDepsCache[resourcePath] = result;
		}

		return m_recursiveDepsCache[resourcePath];
	}

	void GpuResourceCollector::FilterGpuResource( const res::ResourcePath& path, GpuResourceCollector::Resources& resources )
	{
		const red::StringView pathStr = path.ToStringView();

		for ( auto& ext : resources.m_textures.m_extensions )
		{
			if (pathStr.EndsWith(ext))
			{
				resources.m_textures.m_resources.PushBack(path);
				return;
			}
		}

		for (auto& ext : resources.m_meshes.m_extensions)
		{
			if (pathStr.EndsWith(ext))
			{
				resources.m_meshes.m_resources.PushBack(path);
				return;
			}
		}

		for (auto& ext : resources.m_materials.m_extensions)
		{
			if (pathStr.EndsWith(ext))
			{
				resources.m_materials.m_resources.PushBack(path);
				return;
			}
		}

		for (auto& ext : resources.m_videos.m_extensions)
		{
			if (pathStr.EndsWith(ext))
			{
				resources.m_videos.m_resources.PushBack(path);
				return;
			}
		}
	}

	GpuResourceCollector::Resources::Resources()
	{
		m_textures.m_extensions = { ".xbm", ".xcube", ".cubemap", "dtex" };
		m_meshes.m_extensions = { ".mesh", ".w2mesh", ".redphysics", ".physicalscene" };
		m_materials.m_extensions = { ".mlmask" };
		m_videos.m_extensions = { ".bk", ".bik", ".bk2" };
	}
}
