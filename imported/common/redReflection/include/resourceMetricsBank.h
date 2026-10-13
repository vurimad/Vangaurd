/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourceMetrics.h"
#include "../../redReflection/include/resourcePath.h"
#include "../../redReflection/include/scriptable.h"
#include "../../redSystem/include/readWriteSpinLock.h"
#include "../../redContainers/include/hashMap.h"

#ifdef USE_PROFILER
#define USE_RESOURCE_METRICS_BANK
#endif

namespace red { class AbsolutePath; }

namespace res
{
	// Note: not especially meaningful to distinguish whether the resource was loaded from this cache or an external reference kept it alive.
	// Because the subsequent loading requests will get the resource from this cache instead, and it's better to correlate with the number of loading requests.
	// Pending vs Cold/Hot plotted against the number of loading requests can be harder to reason about as well. But it's distinguished so we can tell how often we're doing I/O and deserialization.
	enum LoadingRequestEventType : Uint8
	{
		Failed, // Resource failed to load
		Cold,  // First time resource isn't loading or already loaded
		Pending, // Resources isn't loaded yet, but not the first request
		Hot, // resource is already available from this cache or externally.
		COUNT,
	};

	struct LoadingRequestMetricCounters
	{
		Uint32 numFailed{ 0 };
		Uint32 numCold{ 0 };
		Uint32 numPending{ 0 };
		Uint32 numHot{ 0 };

		Uint32 GetCounter(res::LoadingRequestEventType type) const
		{
			switch (type)
			{
			case LoadingRequestEventType::Failed:	return numFailed;
			case LoadingRequestEventType::Cold:		return numCold;
			case LoadingRequestEventType::Pending:	return numPending;
			case  LoadingRequestEventType::Hot:		return numHot;
			default:
				RED_FATAL("Unexpected type: %u", type);
				break;
			}
			return 0;
		}
	};
	
	using LoadingRequestsMetricsDictionary = red::HashMap< ResourcePath, LoadingRequestMetricCounters >;

	class LoadingRequestsMetricsSnapshot
	{
	public:
		LoadingRequestsMetricsSnapshot()
			: m_shapshotNumber(0)
			, m_metrics{ red::PoolDebug() }
			, m_extensionFilters{ red::PoolDebug() }
		{}

		LoadingRequestsMetricsSnapshot(LoadingRequestsMetricsDictionary&& metrics, const String& snapshotPrefix, Uint32 snapshotNumber, const String& pathFilter, const red::DynArray<String>& extensionFilters)
			: m_metrics( std::move(metrics) )
			, m_snapshotPrefix( snapshotPrefix )
			, m_shapshotNumber( snapshotNumber )
			, m_pathFilter( pathFilter )
			, m_extensionFilters( extensionFilters )
		{
			InitTotalMetrics();
		}

		LoadingRequestsMetricsSnapshot(const LoadingRequestsMetricsDictionary& metrics, const String& snapshotPrefix, Uint32 snapshotNumber, const String& pathFilter, const red::DynArray<String>& extensionFilters)
			: m_metrics(metrics)
			, m_snapshotPrefix( snapshotPrefix )
			, m_shapshotNumber( snapshotNumber )
			, m_pathFilter(pathFilter)
			, m_extensionFilters(extensionFilters)
		{
			InitTotalMetrics();
		}

		LoadingRequestsMetricsSnapshot(const LoadingRequestsMetricsSnapshot&) = default;

		LoadingRequestsMetricsSnapshot& operator=(const LoadingRequestsMetricsSnapshot&) = default;

		LoadingRequestsMetricsSnapshot(LoadingRequestsMetricsSnapshot&&) = default;
		
		LoadingRequestsMetricsSnapshot& operator=(LoadingRequestsMetricsSnapshot&&) = default;

		const LoadingRequestsMetricsDictionary& GetMetrics() const { return m_metrics; }
		const LoadingRequestMetricCounters& GetTotalMetrics() const { return m_totalMetricCounters; }
		const String& GetSnapshotPrefix() const { return m_snapshotPrefix; }
		Uint32 GetSnapshotNumber() const { return m_shapshotNumber; }
		const String& GetPathFilter() const { return m_pathFilter; }
		const red::DynArray<String>& GetExtensionFilters() const { return m_extensionFilters; }

	private:
		void InitTotalMetrics()
		{
			for (const auto& it : m_metrics)
			{
				m_totalMetricCounters.numFailed += it.Value().numFailed;
				m_totalMetricCounters.numCold += it.Value().numCold;
				m_totalMetricCounters.numPending += it.Value().numPending;
				m_totalMetricCounters.numHot += it.Value().numHot;
			}
		}

		LoadingRequestsMetricsDictionary m_metrics;
		LoadingRequestMetricCounters m_totalMetricCounters;
		String m_snapshotPrefix;
		Uint32 m_shapshotNumber;
		String m_pathFilter;
		red::DynArray<String> m_extensionFilters;
	};

	class RED_REFLECTION_API ResourceMetricsBank
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

	public:
		ResourceMetricsBank();
		~ResourceMetricsBank();

		void FetchMetrics( RawResourceMetrics* inputMetrics ) const;

		// --------------------------------------------------------------
		// textures
		void OnLoadTexture( const SingleResourceMetrics& metrics );
		void OnUnloadTexture( const ResourcePath& path );

		void GetTextureMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics ) const;
		void GetTextureMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics, const red::DynArray< res::ResourcePath >& textures ) const;
		SingleResourceMetrics GetTextureMetrics( const ResourcePath& path ) const;

		red::Uint64 GetTextureTotalCPUBytesUsed() const;
		red::Uint64 GetTextureTotalGPUBytesUsed() const;
		red::Uint64 GetTextureGPUBytesUsed( const red::DynArray< res::ResourcePath >& paths ) const;

		// --------------------------------------------------------------
		// meshes
		void OnLoadMesh( const SingleResourceMetrics& metrics );
		void OnUnloadMesh( const ResourcePath& path );

		void GetMeshMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics ) const;
		void GetMeshMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics, const red::DynArray< res::ResourcePath >& meshes ) const;
		SingleResourceMetrics GetMeshMetrics( const ResourcePath& path ) const;

		red::Uint64 GetMeshTotalCPUBytesUsed() const;
		red::Uint64 GetMeshTotalGPUBytesUsed() const;
		red::Uint64 GetMeshGPUBytesUsed( const red::DynArray< res::ResourcePath >& paths ) const;

		// --------------------------------------------------------------
		// materials
		void OnLoadMaterial( const SingleResourceMetrics& metrics );
		void OnUnloadMaterial( const ResourcePath& path );

		void GetMaterialMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics ) const;
		void GetMaterialMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics, const red::DynArray< res::ResourcePath >& materials ) const;
		SingleResourceMetrics GetMaterialMetrics( const ResourcePath& path ) const;

		red::Uint64 GetMaterialTotalCPUBytesUsed() const;
		red::Uint64 GetMaterialTotalGPUBytesUsed() const;
		red::Uint64 GetMaterialGPUBytesUsed( const red::DynArray< res::ResourcePath >& paths ) const;

		// --------------------------------------------------------------
		// videos
		void OnLoadVideo(const SingleResourceMetrics& metrics);
		void OnUnloadVideo(const ResourcePath& path);

		void GetVideoMetrics(red::DynArray< SingleResourceMetrics >& resourceMetrics) const;
		void GetVideoMetrics(red::DynArray< SingleResourceMetrics >& resourceMetrics, const red::DynArray< res::ResourcePath >& materials) const;
		SingleResourceMetrics GetVideoMetrics(const ResourcePath& path) const;

		red::Uint64 GetVideoTotalCPUBytesUsed() const;
		red::Uint64 GetVideoTotalGPUBytesUsed() const;
		red::Uint64 GetVideoGPUBytesUsed(const red::DynArray< res::ResourcePath >& paths) const;

		// --------------------------------------------------------------
		// loading requests
		void GetLoadingRequestSnapshots(red::DynArray<LoadingRequestsMetricsSnapshot>& outSnapshots) const;

		static const Uint32 c_maxSnapshots = 10;

		void OnLoadingRequest(const res::ResourcePath& resourcePath, LoadingRequestEventType type);
		void NextLoadingRequestSnapshot(const String& snapshotPrefix, const String& pathFilter = "", const red::DynArray<String>& extensionFilters = red::DynArray<String>(red::PoolDebug()), Bool resetCounters = true, Uint32 maxSnapshots = c_maxSnapshots);
		LoadingRequestMetricCounters GetCurrentTotalCounters() const;

		struct DeferredDataBufferBeginLoadParams
		{
			res::ResourcePath resourcePath;
			Uint32 bufferOffset{ 0 };
			Uint32 bufferSizeOnDisk{ 0 };
			Bool isInlineRead{ false };
		};

		void OnDeferredDataBufferBeginLoad(const DeferredDataBufferBeginLoadParams& params);
		void GetColdLoadedMetrics(red::HashMap<res::ResourcePath, ColdLoadedMetrics>& coldLoadedMetrics) const;
		void ClearColdLoadedMetrics();

		// --------------------------------------------------------------
		// utils
		void WriteReportToFile( const red::AbsolutePath& filePath ) const;

	private:
		struct InternalResourceMemoryMetrics
		{
			red::Uint32 cpuBytesUsed;
			red::Uint32 gpuBytesUsed;

			struct
			{
				TextureGroup group;
			} texture;

			struct
			{
				Bool isSkinned;
				Bool hasPhysics;
			} mesh;

			red::Atomic<red::Uint32> refCount;

			InternalResourceMemoryMetrics()
				: cpuBytesUsed( 0 )
				, gpuBytesUsed( 0 )
				, texture({ TG_Unknown })
				, mesh({ false, false })
				, refCount( 1 )
			{}

			InternalResourceMemoryMetrics( red::Uint32 cpuBytes, red::Uint32 gpuBytes, TextureGroup group, Bool isSkinned, Bool hasPhysics )
				: cpuBytesUsed( cpuBytes )
				, gpuBytesUsed( gpuBytes )
				, texture({ group })
				, mesh({ isSkinned, hasPhysics })
				, refCount( 1 )
			{}

			InternalResourceMemoryMetrics( const InternalResourceMemoryMetrics& metrics )
				: cpuBytesUsed( metrics.cpuBytesUsed )
				, gpuBytesUsed( metrics.gpuBytesUsed )
			{
				texture.group = metrics.texture.group;
				mesh.isSkinned = metrics.mesh.isSkinned;
				mesh.hasPhysics = metrics.mesh.hasPhysics;
				refCount.SetValue(metrics.refCount.GetValue());
			}

			InternalResourceMemoryMetrics& operator=(const InternalResourceMemoryMetrics& metrics)
			{
				this->cpuBytesUsed = metrics.cpuBytesUsed;
				this->gpuBytesUsed = metrics.gpuBytesUsed;
				this->texture.group = metrics.texture.group;
				this->mesh.isSkinned = metrics.mesh.isSkinned;
				this->mesh.hasPhysics = metrics.mesh.hasPhysics;
				this->refCount.SetValue(metrics.refCount.GetValue());
				return *this;
			}
		};

		using ResourceMetricsDictionary = red::HashMap< ResourcePath, InternalResourceMemoryMetrics >;

		// --------------------------------------------------------------
		// utils
		void OnLoadResource( const SingleResourceMetrics& metrics, red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary );
		void OnUnloadResource( const ResourcePath& path, red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary );
		void GetResourceMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics, red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary ) const;
		void GetResourceMetrics( red::DynArray< SingleResourceMetrics >& resourceMetrics, const red::DynArray< res::ResourcePath >& resources, red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary ) const;
		SingleResourceMetrics GetResourceMetrics( const ResourcePath& path, red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary ) const;

		Uint64 GetResourceTotalCPUBytesUsed( red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary ) const;
		Uint64 GetResourceTotalGPUBytesUsed( red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary ) const;
		Uint64 GetResourceGPUBytesUsed( red::RWSpinLock& metricsLock, ResourceMetricsDictionary& metricsDictionary, const red::DynArray< res::ResourcePath >& paths ) const;

		static SingleResourceMetrics ToResourceMemoryMetrics( const ResourceMetricsDictionary::const_iterator& iter );

		// --------------------------------------------------------------
		// textures
		mutable ResourceMetricsDictionary m_textureMetrics;
		mutable red::RWSpinLock m_textureMetricsLock;
		mutable Uint64 m_annonymousTexturesGpuBytesUsed;

		// --------------------------------------------------------------
		// meshes
		mutable ResourceMetricsDictionary m_meshMetrics;
		mutable red::RWSpinLock m_meshMetricsLock;

		// --------------------------------------------------------------
		// materials
		mutable ResourceMetricsDictionary m_materialMetrics;
		mutable red::RWSpinLock m_materialMetricsLock;

		// --------------------------------------------------------------
		// videos
		mutable ResourceMetricsDictionary m_videoMetrics;
		mutable red::RWSpinLock m_videoMetricsLock;

		// --------------------------------------------------------------
		mutable LoadingRequestsMetricsDictionary m_loadingRequestMetrics;
		mutable red::DynArray< LoadingRequestsMetricsSnapshot > m_loadingRequestMetricsSnapshots;
		mutable LoadingRequestMetricCounters m_totalLoadingRequestMetricCounters;
		mutable String m_loadingRequestMetricsPathFilter;
		mutable red::DynArray<String> m_loadingRequestMetricsExtensionsFilters;
		mutable red::HashMap<res::ResourcePath, ColdLoadedMetrics> m_coldLoadedMetrics;
		mutable red::RWSpinLock m_lodingRequestMetricsLock;
	};

	class RED_REFLECTION_API ResourceMetricsReportGenerator : public IScriptable
	{
		RTTI_DECLARE_TYPE( ResourceMetricsReportGenerator );

		static void funcWriteReportToFile( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	};
}