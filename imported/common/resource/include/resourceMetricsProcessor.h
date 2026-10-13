/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../redReflection/include/resourceMetrics.h"
#include "../../redContainers/include/string/stringBuilder.h"

namespace res
{
	namespace ResourceMetrics
	{
		enum SortedBy : Uint8
		{
			NotSorted,

			GPU_MEMORY_INC,
			GPU_MEMORY_DEC,

			REF_COUNT_INC,
			REF_COUNT_DEC,

			RESOURCE_PATH_ALPHABETICALLY,
			RESOURCE_PATH_INV_ALPHABETICALLY,

			BigGPULowCount,

			Count
		};

		struct RED_RESOURCE_API Settings
		{
			RED_USE_MEMORY_POOL( red::PoolDebug );

			struct Category;
			struct SubCategory;
			struct PathFilter;

			static Settings CreateFromJSON( const String& json );

			red::DynArray< Category > categories{ red::PoolDebug() };

			SortedBy sortedBy = SortedBy::NotSorted;

			Bool useCache = false;

			//////////////////////////////////////////////////////////////////////////

			struct PathFilter
			{
				static constexpr size_t max_path_length = 24u;

				char resourcePath1[max_path_length] = "\0";
				char resourcePath2[max_path_length] = "\0";

				PathFilter( const char* inPath1, const char* inPath2 )
				{
					const size_t path1Len = red::Strlen( inPath1 );
					const size_t path2Len = red::Strlen( inPath2 );

					RED_FATAL_ASSERT( path1Len < max_path_length );
					RED_FATAL_ASSERT( path2Len < max_path_length );

					if ( path1Len > 0 )	red::Strcpy( resourcePath1, inPath1, red::Strlen( inPath1 ) + 1 );
					if ( path2Len > 0 )	red::Strcpy( resourcePath2, inPath2, red::Strlen( inPath2 ) + 1 );
				}

				PathFilter()
					: PathFilter( "\0", "\0" )
				{}
			};

			struct Category
			{
				String name;
				String team;
				Uint64 limit = 1u;
				PathFilter filter;
				ResourceMetricsType resType;
				red::DynArray< SubCategory > subCategories{ red::PoolDebug() };
			};

			struct SubCategory
			{
				String name;
				PathFilter filter;
			};
		};
	}

	class ResourceMetricsProcessorCache;

	class RED_RESOURCE_API ResourceMetricsProcessor
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

	public:
		struct Bucket;
		struct SubBucket;
		struct TeamUsage;


		ResourceMetricsProcessor( const ResourceMetrics::Settings& filteringSettings );
		~ResourceMetricsProcessor();

		void RefreshData( RawResourceMetrics& rawData, const ResourceMetrics::Settings& filteringSettings );

		template< typename Getter >
		void UpdateMeshRefCount( const Getter& refCountGetter );

		void Sort( ResourceMetrics::SortedBy by );

		void Sort( ResourceMetrics::SortedBy by, Uint32 bucketIndex );

		void Sort( ResourceMetrics::SortedBy by, Uint32 bucketIndex, Uint32 subBucketIndex );

		//Accessors
		template< typename Functor >
		void ForEachTeamUsage( const Functor& functor ) const;

		const red::ArraySpan< const Bucket > GetBuckets() const { return m_buckets; }

		const Bucket& GetBucket( Uint32 index ) const { return m_buckets[index]; }
		Bucket& GetBucket( Uint32 index ) { return m_buckets[index]; }

		//Export functions
		String PrintTeamMemoryUsageToCSV() const;
		String PrintTotalMemoryUsageToCSV() const;
		String PrintCategoryDetailedToCSV( Uint32 index ) const;
		String PrintCategoriesDetailedToCSV( red::ArraySpan< Uint32 > indices ) const;
		//String PrintSubCategoryDetailedToCSV( const SubBucket& bucket ) const;

		String PrintTeamMemoryUsageToJSON() const;
		String PrintTotalMemoryUsageToJSON() const;
		String PrintCategoryDetailedToJSON( Uint32 index ) const;
		String PrintCategoriesDetailedToJSON( red::ArraySpan< Uint32 > indices ) const;
		//String PrintSubCategoryDetailedToJSON( const SubBucket& bucket ) const;

	private:

		void InitBucketsFromSettings( const ResourceMetrics::Settings& filteringSettings );
		void ProcessRawDataAndFillBuckets( RawResourceMetrics& rawData, const ResourceMetrics::Settings& filteringSettings );
		void ProcessRawDataType( res::ResourceMetricsType resType, res::RawResourceMetrics::ResType& data, const ResourceMetrics::Settings& filteringSettings );

		void PushToSubBucket( SubBucket& to, const SingleResourceMetrics& entry );

		red::DynArray< Bucket > m_buckets{ red::PoolDebug() };
		red::HashMap< String, TeamUsage > m_teams{ red::PoolDebug() };

		red::UniquePtr< ResourceMetricsProcessorCache > m_cache;

	public:
		struct Bucket
		{
			String name;
			String team;
			Uint64 limit = 1u;
			Uint64 usedGPUBytes = 0u;
			ResourceMetricsType resType;
			red::DynArray< SubBucket > subBuckets{ red::PoolDebug() };
		};

		struct SubBucket
		{
			String name;
			Uint64 usedGPUBytes = 0u;
			ResourceMetrics::SortedBy sortedBy = ResourceMetrics::GPU_MEMORY_DEC;
			red::DynArray< SingleResourceMetrics > entries{ red::PoolDebug() };
		};

		struct TeamUsage
		{
			Uint64 gpuBytesLimit;
			Uint64 gpuBytesUsed;
		};
	};


	template< typename Getter >
	void ResourceMetricsProcessor::UpdateMeshRefCount( const Getter& refCountGetter )
	{
		for ( Uint32 i = 0; i < m_buckets.Size(); ++i )
		{
			auto& bucket = m_buckets[i];
			if ( bucket.resType == ResourceMetricsType::Mesh )
			{
				for ( Uint32 j = 0; j < bucket.subBuckets.Size(); ++j )
				{
					auto& subBucket = bucket.subBuckets[j];

					for ( auto& entry : subBucket.entries )
					{
						std::pair< Uint32, Uint32 > total_InRangePair = refCountGetter( entry.path );
						entry.refCount = total_InRangePair.first;
						entry.mesh.inRange = total_InRangePair.second;
					}

					Sort( subBucket.sortedBy, i, j );
				}
			}
		}
	}

	template< typename Functor >
	void ResourceMetricsProcessor::ForEachTeamUsage( const Functor& functor ) const
	{
		for ( auto& teamUsage : m_teams )
		{
			functor( teamUsage.Key(), teamUsage.Value().gpuBytesUsed, teamUsage.Value().gpuBytesLimit );
		}
	}
}