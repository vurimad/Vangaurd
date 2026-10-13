#include "build.h"
#include "resourceMetricsBank.h"
#include "textWriter.h"
#include "../../redContainers/include/string/stringBuilder.h"
#include "resourceLoader.h"
#include "../../redCore/include/absolutePath.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redReflection/include/scriptStackFrame.h"
#include "../../redReflection/include/redReflectionPublic.h"
#include "../../redSystem/include/redThreadsAtomic.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( ResourceMetricsReportGenerator, res );
	RTTI_PARENT_TYPE( IScriptable );
	RTTI_SCRIPT_ALIAS( "ResourceMetricsReportGenerator" );
	RTTI_NATIVE_STATIC_FUNCTION( "WriteReportToFile", funcWriteReportToFile );
RTTI_END_TYPE();

namespace res
{
namespace
{
	class JsonWriter
	{
	public:
		JsonWriter( FILE* file )
			: m_file( file )
		{}
		
		void BeginObject()
		{
			fprintf( m_file, "{" );
			m_itemCounts.PushBack( 0 );
		}

		void EndObject()
		{
			fprintf( m_file, "}" );
			m_itemCounts.PopBack();
		}

		void BeginArray()
		{
			fprintf( m_file, "[" );
			m_itemCounts.PushBack( 0 );
		}

		void EndArray()
		{
			fprintf( m_file, "]" );
			m_itemCounts.PopBack();
		}

		void BeginArrayElement()
		{
			auto& count = m_itemCounts.Back();
			if ( count++ > 0 )
			{
				fprintf( m_file, "," );
			}
		}

		void EndArrayElement()
		{
			// nothing
		}

		void WriteKey( const String& key )
		{
			fprintf( m_file, "\"%s\":", key.AsChar() );
		}

		void WriteValue( const String& str )
		{
			fprintf( m_file, "\"%s\"", str.AsChar() );
		}

		void WriteValue( Uint32 value )
		{
			fprintf( m_file, "%lu", value );
		}

		void NextProperty()
		{
			fprintf( m_file, "," );
		}

	private:
		FILE* m_file;
		red::DynArray< Uint32 > m_itemCounts{ red::PoolDebug() };
	};
}

	ResourceMetricsBank::ResourceMetricsBank()
		: m_textureMetrics( red::PoolDebug() )
		, m_meshMetrics( red::PoolDebug() )
		, m_materialMetrics( red::PoolDebug() )
		, m_videoMetrics( red::PoolDebug() )
		, m_loadingRequestMetrics( red::PoolDebug() )
		, m_loadingRequestMetricsSnapshots( red::PoolDebug() )
		, m_loadingRequestMetricsExtensionsFilters( red::PoolDebug() )
		, m_coldLoadedMetrics( red::PoolDebug() )
	{}

	ResourceMetricsBank::~ResourceMetricsBank()
	{}

	//////////////////////////////////////////////////////////////////////////

	void ResourceMetricsBank::FetchMetrics( RawResourceMetrics* inputMetrics ) const
	{
		inputMetrics->textures.metrics.Clear();
		GetTextureMetrics( inputMetrics->textures.metrics );
		inputMetrics->textures.usedCPUBytes = GetTextureTotalCPUBytesUsed();
		inputMetrics->textures.usedGPUBytes = GetTextureTotalGPUBytesUsed();

		inputMetrics->meshes.metrics.Clear();
		GetMeshMetrics( inputMetrics->meshes.metrics );
		inputMetrics->meshes.usedCPUBytes = GetMeshTotalCPUBytesUsed();
		inputMetrics->meshes.usedGPUBytes = GetMeshTotalGPUBytesUsed();

		inputMetrics->materials.metrics.Clear();
		GetMaterialMetrics( inputMetrics->materials.metrics );
		inputMetrics->materials.usedCPUBytes = GetMaterialTotalCPUBytesUsed();
		inputMetrics->materials.usedGPUBytes = GetMaterialTotalGPUBytesUsed();

		inputMetrics->videos.metrics.Clear();
		GetVideoMetrics( inputMetrics->videos.metrics );
		inputMetrics->videos.usedCPUBytes = GetVideoTotalCPUBytesUsed();
		inputMetrics->videos.usedGPUBytes = GetVideoTotalGPUBytesUsed();
	}

	// --------------------------------------------------------------
	// textures

	void ResourceMetricsBank::OnLoadTexture( const SingleResourceMetrics& metrics )
	{
		OnLoadResource( metrics, m_textureMetricsLock, m_textureMetrics );
	}

	void ResourceMetricsBank::OnUnloadTexture( const ResourcePath& path )
	{
		OnUnloadResource( path, m_textureMetricsLock, m_textureMetrics );
	}

	void ResourceMetricsBank::GetTextureMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics ) const
	{
		GetResourceMetrics( resourceMetrics, m_textureMetricsLock, m_textureMetrics );
	}

	void ResourceMetricsBank::GetTextureMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics, const red::DynArray< res::ResourcePath >& textures ) const
	{
		GetResourceMetrics( resourceMetrics, textures, m_textureMetricsLock, m_textureMetrics );
	}

	SingleResourceMetrics ResourceMetricsBank::GetTextureMetrics( const ResourcePath& path ) const
	{
		return GetResourceMetrics( path, m_textureMetricsLock, m_textureMetrics );
	}

	red::Uint64 ResourceMetricsBank::GetTextureTotalCPUBytesUsed() const
	{
		return GetResourceTotalCPUBytesUsed( m_textureMetricsLock, m_textureMetrics );
	}

	red::Uint64 ResourceMetricsBank::GetTextureTotalGPUBytesUsed() const
	{
		return GetResourceTotalGPUBytesUsed( m_textureMetricsLock, m_textureMetrics ) + m_annonymousTexturesGpuBytesUsed;
	}

	red::Uint64 ResourceMetricsBank::GetTextureGPUBytesUsed( const red::DynArray< res::ResourcePath >& paths ) const
	{
		return GetResourceGPUBytesUsed( m_textureMetricsLock, m_textureMetrics, paths );
	}

	// --------------------------------------------------------------
	// meshes

	void ResourceMetricsBank::OnLoadMesh( const SingleResourceMetrics& metrics )
	{
		OnLoadResource( metrics, m_meshMetricsLock, m_meshMetrics );
	}

	void ResourceMetricsBank::OnUnloadMesh( const ResourcePath& path )
	{
		OnUnloadResource( path, m_meshMetricsLock, m_meshMetrics );
	}

	void ResourceMetricsBank::GetMeshMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics ) const
	{
		GetResourceMetrics( resourceMetrics, m_meshMetricsLock, m_meshMetrics );
	}

	void ResourceMetricsBank::GetMeshMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics, const red::DynArray< res::ResourcePath >& meshes ) const
	{
		GetResourceMetrics( resourceMetrics, meshes, m_meshMetricsLock, m_meshMetrics );
	}

	SingleResourceMetrics ResourceMetricsBank::GetMeshMetrics( const ResourcePath& path ) const
	{
		return GetResourceMetrics( path, m_meshMetricsLock, m_meshMetrics );
	}

	red::Uint64 ResourceMetricsBank::GetMeshTotalCPUBytesUsed() const
	{
		return GetResourceTotalCPUBytesUsed( m_meshMetricsLock, m_meshMetrics );
	}

	red::Uint64 ResourceMetricsBank::GetMeshTotalGPUBytesUsed() const
	{
		return GetResourceTotalGPUBytesUsed( m_meshMetricsLock, m_meshMetrics );
	}

	red::Uint64 ResourceMetricsBank::GetMeshGPUBytesUsed( const red::DynArray< res::ResourcePath >& paths ) const
	{
		return GetResourceGPUBytesUsed( m_meshMetricsLock, m_meshMetrics, paths );
	}

	// --------------------------------------------------------------
	// materials

	void ResourceMetricsBank::OnLoadMaterial( const SingleResourceMetrics& metrics )
	{
		OnLoadResource( metrics, m_materialMetricsLock, m_materialMetrics );
	}

	void ResourceMetricsBank::OnUnloadMaterial( const ResourcePath& path )
	{
		OnUnloadResource( path, m_materialMetricsLock, m_materialMetrics );
	}

	void ResourceMetricsBank::GetMaterialMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics ) const
	{
		GetResourceMetrics( resourceMetrics, m_materialMetricsLock, m_materialMetrics );
	}

	void ResourceMetricsBank::GetMaterialMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics, const red::DynArray< res::ResourcePath >& materials ) const
	{
		GetResourceMetrics( resourceMetrics, materials, m_materialMetricsLock, m_materialMetrics );
	}

	SingleResourceMetrics ResourceMetricsBank::GetMaterialMetrics( const ResourcePath& path ) const
	{
		return GetResourceMetrics( path, m_materialMetricsLock, m_materialMetrics );
	}

	red::Uint64 ResourceMetricsBank::GetMaterialTotalCPUBytesUsed() const
	{
		return GetResourceTotalCPUBytesUsed( m_materialMetricsLock, m_materialMetrics );
	}

	red::Uint64 ResourceMetricsBank::GetMaterialTotalGPUBytesUsed() const
	{
		return GetResourceTotalGPUBytesUsed( m_materialMetricsLock, m_materialMetrics );
	}

	red::Uint64 ResourceMetricsBank::GetMaterialGPUBytesUsed( const red::DynArray< res::ResourcePath >& paths ) const
	{
		return GetResourceGPUBytesUsed( m_materialMetricsLock, m_materialMetrics, paths );
	}

	// --------------------------------------------------------------
	// videos

	void ResourceMetricsBank::OnLoadVideo(const SingleResourceMetrics& metrics)
	{
		OnLoadResource(metrics, m_videoMetricsLock, m_videoMetrics);
	}

	void ResourceMetricsBank::OnUnloadVideo(const ResourcePath& path)
	{
		OnUnloadResource(path, m_videoMetricsLock, m_videoMetrics);
	}

	void ResourceMetricsBank::GetVideoMetrics(red::DynArray< SingleResourceMetrics >& resourceMetrics) const
	{
		GetResourceMetrics(resourceMetrics, m_videoMetricsLock, m_videoMetrics);
	}

	void ResourceMetricsBank::GetVideoMetrics(red::DynArray< SingleResourceMetrics >& resourceMetrics, const red::DynArray< res::ResourcePath >& videos) const
	{
		GetResourceMetrics(resourceMetrics, videos, m_videoMetricsLock, m_videoMetrics);
	}

	SingleResourceMetrics ResourceMetricsBank::GetVideoMetrics(const ResourcePath& path) const
	{
		return GetResourceMetrics(path, m_videoMetricsLock, m_videoMetrics);
	}

	red::Uint64 ResourceMetricsBank::GetVideoTotalCPUBytesUsed() const
	{
		return GetResourceTotalCPUBytesUsed(m_videoMetricsLock, m_videoMetrics);
	}

	red::Uint64 ResourceMetricsBank::GetVideoTotalGPUBytesUsed() const
	{
		return GetResourceTotalGPUBytesUsed(m_videoMetricsLock, m_videoMetrics);
	}

	red::Uint64 ResourceMetricsBank::GetVideoGPUBytesUsed(const red::DynArray< res::ResourcePath >& paths) const
	{
		return GetResourceGPUBytesUsed(m_videoMetricsLock, m_videoMetrics, paths);
	}

	// --------------------------------------------------------------
	// loading requests

	void ResourceMetricsBank::GetLoadingRequestSnapshots(red::DynArray<LoadingRequestsMetricsSnapshot>& outSnapshots) const
	{
		RED_SCOPE_SHARED_LOCK(m_lodingRequestMetricsLock);
		outSnapshots.EmplaceBack(m_loadingRequestMetrics, "<Current>", 0, m_loadingRequestMetricsPathFilter, m_loadingRequestMetricsExtensionsFilters);
		outSnapshots.PushBack( m_loadingRequestMetricsSnapshots );
	}

	namespace helper
	{
		static void UpdateLoadingMetricCounters(LoadingRequestMetricCounters& counters, LoadingRequestEventType type)
		{
			switch (type)
			{
			case LoadingRequestEventType::Failed:
				atomic::Increment(&counters.numFailed);
				break;
			case LoadingRequestEventType::Cold:
				atomic::Increment(&counters.numCold);
				break;
			case LoadingRequestEventType::Pending:
				atomic::Increment(&counters.numPending);
				break;
			case  LoadingRequestEventType::Hot:
				atomic::Increment(&counters.numHot);
				break;
			default:
				RED_FATAL("Unexpected type: %u", type);
				break;
			}
		}
	}

	// --------------------------------------------------------------
	// loading requests
	void ResourceMetricsBank::OnLoadingRequest(const res::ResourcePath& resourcePath, LoadingRequestEventType type)
	{
		if (!resourcePath.IsValid())
		{
			return;
		}

		if (type == LoadingRequestEventType::Cold)
		{
			RED_SCOPE_LOCK(m_lodingRequestMetricsLock);
			m_coldLoadedMetrics[resourcePath].resourceLoadedCount += 1;
		}

		// SHARED lock
		{
			RED_SCOPE_SHARED_LOCK(m_lodingRequestMetricsLock);

			Bool pathExtensionAllowed = m_loadingRequestMetricsExtensionsFilters.Empty();
			for (const auto& ext : m_loadingRequestMetricsExtensionsFilters)
			{
				if (ext == red::utils::GetExtension(resourcePath.ToStringView()))
				{
					pathExtensionAllowed = true;
					break;
				}
			}

			if ( !pathExtensionAllowed || resourcePath.ToStringView().Find( m_loadingRequestMetricsPathFilter ) == red::StringView::npos )
			{
				return;
			}

			helper::UpdateLoadingMetricCounters(m_totalLoadingRequestMetricCounters, type);

			if (m_loadingRequestMetrics.KeyExist(resourcePath))
			{
				auto& metricCounters = m_loadingRequestMetrics[resourcePath];
				helper::UpdateLoadingMetricCounters(metricCounters, type);
				return;
			}
		}

		// EXCLUSIVE lock
		{
			RED_SCOPE_LOCK(m_lodingRequestMetricsLock);

			auto& metricsCounters = m_loadingRequestMetrics[resourcePath];
			helper::UpdateLoadingMetricCounters(metricsCounters, type);
		}
	}

	namespace helper
	{
		static Uint32 GetNextSnapshotNumberForPrefix(const String& snapshotPrefix, red::DynArray< LoadingRequestsMetricsSnapshot >& snapshots)
		{
			Uint32 snapshotNumber = 0;
			for (const auto& it : snapshots)
			{
				if (it.GetSnapshotPrefix() == snapshotPrefix)
				{
					if (it.GetSnapshotNumber() >= snapshotNumber)
					{
						snapshotNumber = it.GetSnapshotNumber() + 1;
					}
				}
			}
			return snapshotNumber;
		}
	}

	namespace helper
	{
		static void CleanupExtensionFilters(red::DynArray<String>& extensionFilters)
		{
			for (auto& it : extensionFilters)
			{
				it.Trim();

				if (it == "*.*")
				{
					extensionFilters.Clear();
					return;
				}

				if (it.BeginsWith("*"))
				{
					it = it.StringAfter("*");
					it.Trim();
				}

				if (it.BeginsWith("."))
				{
					it = it.StringAfter(".");
					it.Trim();
				}
			}

			auto removeIt = std::remove_if(extensionFilters.Begin(), extensionFilters.End(), [](const String& it) {
				return it.Empty();
			});

			extensionFilters.Remove(removeIt, extensionFilters.End());
		}
	}

	void ResourceMetricsBank::NextLoadingRequestSnapshot(const String& snapshotPrefix, const String& pathFilter, const red::DynArray<String>& extensionFilters, Bool resetCounters, Uint32 maxSnapshots /*= 3*/)
	{
		RED_SCOPE_LOCK(m_lodingRequestMetricsLock);

		m_totalLoadingRequestMetricCounters = LoadingRequestMetricCounters();

		if (m_loadingRequestMetricsSnapshots.Size() + 1 > maxSnapshots)
		{
			m_loadingRequestMetricsSnapshots.PopBack();
		}

		const Uint32 snapshotNumber = helper::GetNextSnapshotNumberForPrefix(snapshotPrefix, m_loadingRequestMetricsSnapshots);

		if (resetCounters)
		{
			m_loadingRequestMetricsSnapshots.EmplaceAt(0, std::move(m_loadingRequestMetrics), snapshotPrefix, snapshotNumber, m_loadingRequestMetricsPathFilter, m_loadingRequestMetricsExtensionsFilters);
		}
		else
		{
			m_loadingRequestMetricsSnapshots.EmplaceAt(0, m_loadingRequestMetrics,snapshotPrefix, snapshotNumber, m_loadingRequestMetricsPathFilter, m_loadingRequestMetricsExtensionsFilters);
		}

		m_loadingRequestMetricsPathFilter = pathFilter.TrimCopy();
		m_loadingRequestMetricsExtensionsFilters = extensionFilters;
		helper::CleanupExtensionFilters(m_loadingRequestMetricsExtensionsFilters);
	}

	res::LoadingRequestMetricCounters ResourceMetricsBank::GetCurrentTotalCounters() const
	{
		// EXCLUSIVE LOCK - need to atomically copy all counters for self-consistency of stats, which themselves are atomically updated under a shared lock
		RED_SCOPE_LOCK(m_lodingRequestMetricsLock);
		return m_totalLoadingRequestMetricCounters;
	}

	void ResourceMetricsBank::OnDeferredDataBufferBeginLoad(const DeferredDataBufferBeginLoadParams& params)
	{
		RED_SCOPE_LOCK(m_lodingRequestMetricsLock);

		DeferredDataBufferMetrics& entry = m_coldLoadedMetrics[params.resourcePath].m_ddbMetrics;
		auto& segmentEntry = entry.m_offsetToSegments[params.bufferOffset];
		segmentEntry.loadedCount += 1;

		// Should always be the same
		segmentEntry.isInline = params.isInlineRead;
		segmentEntry.sizeOnDisk = params.bufferSizeOnDisk;
	}

	void ResourceMetricsBank::GetColdLoadedMetrics(red::HashMap<res::ResourcePath, ColdLoadedMetrics>& coldLoadedMetrics) const
	{
		RED_SCOPE_SHARED_LOCK(m_lodingRequestMetricsLock);
		coldLoadedMetrics = m_coldLoadedMetrics;
	}

	void ResourceMetricsBank::ClearColdLoadedMetrics()
	{
		RED_SCOPE_LOCK(m_lodingRequestMetricsLock);
		m_coldLoadedMetrics.Clear();
	}

	// --------------------------------------------------------------
	// utils

	void ResourceMetricsBank::WriteReportToFile( const red::AbsolutePath& filePath ) const
	{
#if defined( RED_PLATFORM_LINUX )
		FILE* file = fopen( filePath.AsChar(), "w" );
#else
		FILE* file = nullptr;
		fopen_s( &file, filePath.AsChar(), "w" );
#endif
		if ( !file )
		{
			RED_LOG_ERROR( "Failed to create file" );
			return;
		}

		JsonWriter writer{ file };

		writer.BeginObject();

		{
			writer.WriteKey( "textures" );

			RED_SCOPE_SHARED_LOCK( m_textureMetricsLock );

			writer.BeginArray();

			for ( const auto& texture : m_textureMetrics )
			{
				writer.BeginArrayElement();

				writer.BeginObject();

				writer.WriteKey( "path" );
				auto path = texture.Key().ToString();
				path.ReplaceAll("\\", "\\\\");
				writer.WriteValue( path );

				writer.NextProperty();

				writer.WriteKey( "group" );
				switch ( texture.Value().texture.group )
				{
				case TG_Generic:
					writer.WriteValue( "generic" );
					break;

				case TG_Multilayer:
					writer.WriteValue( "multilayer" );
					break;

				case TG_System:
					writer.WriteValue( "system" );
					break;

				case TG_Unknown:
				default:
					writer.WriteValue( "unknown" );
					break;
				}

				writer.NextProperty();

				writer.WriteKey( "gpu_bytes_used" );
				writer.WriteValue( texture.Value().gpuBytesUsed );

				writer.NextProperty();

				writer.WriteKey( "ref_count" );
				writer.WriteValue( texture.Value().refCount.GetValue() );

				writer.EndObject();

				writer.EndArrayElement();
			}

			writer.EndArray();
		}

		writer.NextProperty();

		{
			writer.WriteKey( "materials" );

			RED_SCOPE_SHARED_LOCK( m_materialMetricsLock );

			writer.BeginArray();

			for ( const auto& material : m_materialMetrics )
			{
				writer.BeginArrayElement();

				writer.BeginObject();

				writer.WriteKey( "path" );
				auto path = material.Key().ToString();
				path.ReplaceAll("\\", "\\\\");
				writer.WriteValue( path );

				writer.NextProperty();

				writer.WriteKey( "gpu_bytes_used" );
				writer.WriteValue( material.Value().gpuBytesUsed );

				writer.NextProperty();

				writer.WriteKey( "ref_count" );
				writer.WriteValue( material.Value().refCount.GetValue() );

				writer.EndObject();

				writer.EndArrayElement();
			}

			writer.EndArray();
		}

		writer.NextProperty();

		{
			writer.WriteKey( "meshes" );

			RED_SCOPE_SHARED_LOCK( m_meshMetricsLock );

			writer.BeginArray();

			for (const auto& mesh : m_meshMetrics )
			{
				writer.BeginArrayElement();

				writer.BeginObject();

				writer.WriteKey( "path" );
				auto path = mesh.Key().ToString();
				path.ReplaceAll( "\\", "\\\\" );
				writer.WriteValue( path );

				writer.NextProperty();

				writer.WriteKey( "is_skinned" );
				writer.WriteValue( mesh.Value().mesh.isSkinned );

				writer.NextProperty();

				writer.WriteKey( "gpu_bytes_used" );
				writer.WriteValue( mesh.Value().gpuBytesUsed );

				writer.NextProperty();

				writer.WriteKey( "ref_count" );
				writer.WriteValue( mesh.Value().refCount.GetValue() );

				writer.EndObject();

				writer.EndArrayElement();
			}

			writer.EndArray();
		}

		writer.EndObject();

		fflush( file );
		fclose( file );
	}

	void ResourceMetricsBank::OnLoadResource( const SingleResourceMetrics& metrics, red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary )
	{
		RED_SCOPE_LOCK( metricsLock );
		RED_FATAL_ASSERT( metrics.gpuBytesUsed > 0 );
		RED_FATAL_ASSERT( metrics.path.IsValid() );

		if( metricsDictionary.KeyExist( metrics.path ) )
		{
			metricsDictionary[ metrics.path ].refCount.Increment();
		}
		else
		{
			metricsDictionary[ metrics.path ] = InternalResourceMemoryMetrics( metrics.cpuBytesUsed, metrics.gpuBytesUsed, metrics.texture.group, metrics.mesh.isSkinned, metrics.mesh.hasPhysics );
		}
	}

	void ResourceMetricsBank::OnUnloadResource( const ResourcePath& path, red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary )
	{
		RED_SCOPE_LOCK( metricsLock );
		RED_FATAL_ASSERT( metricsDictionary.KeyExist( path ), "Could not find '%hs' (%016llX) in the metrics bank when unloading", path.ToDebugString(), path.GetHash() );
		if( !metricsDictionary[ path ].refCount.Decrement() )
		{
			metricsDictionary.Remove( path );
		}
	}

	void ResourceMetricsBank::GetResourceMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics, red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary ) const
	{
		RED_SCOPE_SHARED_LOCK( metricsLock );

		resourceMetrics.Reserve( metricsDictionary.Size() );

		for( auto iter = metricsDictionary.Begin(); iter != metricsDictionary.End(); ++iter )
		{
			resourceMetrics.PushBack( ToResourceMemoryMetrics( iter ) );
		}
	}

	void ResourceMetricsBank::GetResourceMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics, const red::DynArray< res::ResourcePath >& resources, red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary ) const
	{
		RED_SCOPE_SHARED_LOCK( metricsLock );

		resourceMetrics.Reserve( resources.Size() );

		for( const auto resource : resources )
		{
			auto it = metricsDictionary.Find( resource );
			if ( it != metricsDictionary.End() )
			{
				resourceMetrics.PushBack( ToResourceMemoryMetrics( it ) );
			}
		}
	}

	SingleResourceMetrics ResourceMetricsBank::GetResourceMetrics( const ResourcePath& path, red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary ) const
	{
		RED_SCOPE_SHARED_LOCK( metricsLock );

		const auto iter = metricsDictionary.Find( path );
		if ( iter == metricsDictionary.End() )
		{
			return SingleResourceMetrics();
		}

		const auto& internalMetrics = iter.Value();
		return {
			path,
			internalMetrics.cpuBytesUsed,
			internalMetrics.gpuBytesUsed,
			internalMetrics.refCount.GetValue(),
			{ internalMetrics.texture.group },
			{ internalMetrics.mesh.isSkinned, internalMetrics.mesh.hasPhysics }
		};
	}

	red::Uint64 ResourceMetricsBank::GetResourceTotalCPUBytesUsed( red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary ) const
	{
		red::Uint64 totalBytesUsed = 0;
		RED_SCOPE_SHARED_LOCK( metricsLock );
		for( const auto& metrics : metricsDictionary )
		{
			totalBytesUsed += ( metrics.Value().cpuBytesUsed * metrics.Value().refCount.GetValue() );
		}

		return totalBytesUsed;
	}

	red::Uint64 ResourceMetricsBank::GetResourceTotalGPUBytesUsed( red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary ) const
	{
		red::Uint64 totalBytesUsed = 0;
		RED_SCOPE_SHARED_LOCK( metricsLock );
		for( const auto& metrics : metricsDictionary )
		{
			totalBytesUsed += ( metrics.Value().gpuBytesUsed * metrics.Value().refCount.GetValue() );
		}

		return totalBytesUsed;
	}

	Uint64 ResourceMetricsBank::GetResourceGPUBytesUsed( red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary, const red::DynArray< res::ResourcePath >& paths ) const
	{
		red::Uint64 totalBytesUsed = 0;
		RED_SCOPE_SHARED_LOCK( metricsLock );
		for( const auto& path : paths )
		{
			auto it = metricsDictionary.Find( path );
			if ( it != metricsDictionary.End() )
			{
				totalBytesUsed += ( it.Value().gpuBytesUsed * it.Value().refCount.GetValue() );
			}
		}

		return totalBytesUsed;
	}

	SingleResourceMetrics ResourceMetricsBank::ToResourceMemoryMetrics( const ResourceMetricsDictionary::const_iterator& iter )
	{
		const ResourcePath& path = iter.Key();
		const Uint32 cpuBytesUsed = iter.Value().cpuBytesUsed;
		const Uint32 gpuBytesUsed = iter.Value().gpuBytesUsed;
		const Uint32 refCount = iter.Value().cpuBytesUsed;
		const TextureGroup textureGroup = iter.Value().texture.group;
		const Bool meshIsSkinned = iter.Value().mesh.isSkinned;
		const Bool meshHasPhysics = iter.Value().mesh.hasPhysics;

		SingleResourceMetrics returnValue;
		returnValue.path = path;
		returnValue.cpuBytesUsed = cpuBytesUsed;
		returnValue.gpuBytesUsed = gpuBytesUsed;
		returnValue.refCount = refCount;
		returnValue.texture.group = textureGroup;
		returnValue.mesh.isSkinned = meshIsSkinned;
		returnValue.mesh.hasPhysics = meshHasPhysics;

		return returnValue;
	}

	void ResourceMetricsReportGenerator::funcWriteReportToFile(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
	{
		GET_PARAMETER( red::String, fileName, red::String( "resourceReport.json" ) );
		FINISH_PARAMETERS;

		if( GResourceLoader->GetResourceMetricsBank() )
		{
			red::AbsolutePath filePath = red::paths::GetTempDirectory();
			filePath = filePath.AppendFilePath( fileName );
			GResourceLoader->GetResourceMetricsBank()->WriteReportToFile( filePath );
		}

		RETURN_VOID();
	}

}