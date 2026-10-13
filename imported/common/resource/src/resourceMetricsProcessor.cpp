/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"

#include "resourceMetricsProcessor.h"
#include "../../resource/include/jsonSerializer.h"
#include "../../redContainers/include/string/stringUtils.h"

using RapidJsonWriter = rapidjson::PrettyWriter< serialization::json::JsonTextWriter, rapidjson::UTF8< red::AnsiChar >, rapidjson::UTF8< red::AnsiChar >, res::json::JSONAllocator >;
using RapidJsonDocument = rapidjson::GenericDocument< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< res::json::JSONAllocator >, res::json::JSONAllocator >;
using RapidJsonValue = rapidjson::GenericValue< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< res::json::JSONAllocator > >;


namespace res
{
	namespace helper
	{
		ResourceMetricsType ResTypeStringToEnum( const char* typeName )
		{
			if ( red::Strcmp( typeName, "Mesh" ) == 0 )
			{
				return ResourceMetricsType::Mesh;
			}
			else if ( red::Strcmp( typeName, "Material" ) == 0 )
			{
				return ResourceMetricsType::Material;
			}
			else if ( red::Strcmp( typeName, "Texture" ) == 0 )
			{
				return ResourceMetricsType::Texture;
			}
			else if ( red::Strcmp( typeName, "Video" ) == 0 )
			{
				return ResourceMetricsType::Video;
			}
			else
			{
				RED_LOG_ERROR( "Memory metrics: Parsed invalid resource type." );
				return ResourceMetricsType::COUNT;
			}
		}
	}

	namespace ResourceMetrics
	{
		Settings Settings::CreateFromJSON( const String& json )
		{
			Settings outputSettings;

			RapidJsonDocument reader;
			reader.Parse( json.AsChar() );

			if ( reader.HasParseError() )
				return outputSettings;

			const auto gpuMetricsArray = reader["GPUMetrics"].GetArray();

			for ( const auto& gpuMetric : gpuMetricsArray )
			{
				if ( !gpuMetric.HasMember( "shortName" ) ||
					!gpuMetric.HasMember( "longName" ) ||
					!gpuMetric.HasMember( "filter" ) ||
					!gpuMetric.HasMember( "secFilter" ) ||
					!gpuMetric.HasMember( "resType" ) ||
					!gpuMetric.HasMember( "limitMB" ) ||
					!gpuMetric.HasMember( "team" )
					)
				{
					RED_LOG_ERROR( "Bad GPU resource metric parsed. Skipping." );
					continue;
				}

				const char* shortName = gpuMetric["shortName"].GetString();
				const char* longName = gpuMetric["longName"].GetString();
				const char* filter = gpuMetric["filter"].GetString();
				const char* secondaryFilter = gpuMetric["secFilter"].GetString();
				const char* resType = gpuMetric["resType"].GetString();
				const Uint64 limitInBytes = RED_MEGA_BYTE( ((Uint64)gpuMetric["limitMB"].GetUint()) );
				const char* team = gpuMetric["team"].GetString();

				auto& newCategory = outputSettings.categories.EmplaceBack();

				newCategory.name = longName;
				red::Strcpy( newCategory.filter.resourcePath1, filter, RED_ARRAY_COUNT_U32( newCategory.filter.resourcePath1 ) );
				red::Strcpy( newCategory.filter.resourcePath2, secondaryFilter, RED_ARRAY_COUNT_U32( newCategory.filter.resourcePath2 ) );
				newCategory.resType = helper::ResTypeStringToEnum( resType );
				newCategory.team = team;
				newCategory.limit = limitInBytes;
				
				{
					auto& defaultSubCategory = newCategory.subCategories.EmplaceBack();
					defaultSubCategory.name = "Default";
				}

				if ( gpuMetric.HasMember( "subCategories" ) )
				{
					const auto subCategories = gpuMetric["subCategories"].GetArray();
					for ( const auto& subCategory : subCategories )
					{
						if ( !subCategory.HasMember( "name" ) ||
							!subCategory.HasMember( "filter" ) ||
							!subCategory.HasMember( "secFilter" )
							)
						{
							RED_LOG_ERROR( "Bad subcategory parsed. Skipping." );
							continue;
						}

						const char* subCatName = subCategory["name"].GetString();
						const char* subCatFilter = subCategory["filter"].GetString();
						const char* subCatSecondaryFilter = subCategory["secFilter"].GetString();

						auto& newSubCategory = newCategory.subCategories.EmplaceBack();
						newSubCategory.name = subCatName;
						red::Strcpy( newSubCategory.filter.resourcePath1, subCatFilter, RED_ARRAY_COUNT_U32( newSubCategory.filter.resourcePath1 ) );
						red::Strcpy( newSubCategory.filter.resourcePath2, subCatSecondaryFilter, RED_ARRAY_COUNT_U32( newSubCategory.filter.resourcePath2 ) );
					}
				}
			}

			return outputSettings;
		}
	}

	//////////////////////////////////////////////////////////////////////////

	class ResourceMetricsProcessorCache
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );
	public:
		ResourceMetricsProcessorCache();
		~ResourceMetricsProcessorCache();

		using Pos = std::tuple< Uint32, Uint32, String >;

		red::Optional< Pos > Find( const ResourcePath& resPath );
		void Add( const ResourcePath& resPath, Uint32, Uint32, String );

		void Clear();

	private:
		red::RWSpinLock m_lock;
		red::HashMap< ResourcePath, Pos > m_map = { red::PoolDebug() };
	};

	ResourceMetricsProcessorCache::ResourceMetricsProcessorCache() = default;
	ResourceMetricsProcessorCache::~ResourceMetricsProcessorCache() = default;

	red::Optional< ResourceMetricsProcessorCache::Pos > ResourceMetricsProcessorCache::Find( const ResourcePath& resPath )
	{
		RED_SCOPE_SHARED_LOCK( m_lock );
		auto iter = m_map.Find( resPath );

		return iter != m_map.End() ? red::MakeOptional< Pos >( iter.Value() ) : red::MakeEmptyOptional< Pos >();
	}

	void ResourceMetricsProcessorCache::Add( const ResourcePath& resPath, Uint32 b, Uint32 sb, String team )
	{
		RED_SCOPE_LOCK( m_lock );
		m_map.Insert( resPath, Pos( b, sb, team ) );
	}

	void ResourceMetricsProcessorCache::Clear()
	{
		RED_SCOPE_LOCK( m_lock );
		m_map.Clear();
	}

	//////////////////////////////////////////////////////////////////////////

	ResourceMetricsProcessor::ResourceMetricsProcessor( const ResourceMetrics::Settings& filteringSettings )
	{
		InitBucketsFromSettings( filteringSettings );
	
		if ( filteringSettings.useCache )
			m_cache = red::CreateUniquePtr< ResourceMetricsProcessorCache >();
	}

	ResourceMetricsProcessor::~ResourceMetricsProcessor() = default;


	void ResourceMetricsProcessor::RefreshData( RawResourceMetrics& rawData, const ResourceMetrics::Settings& filteringSettings )
	{
		ProcessRawDataAndFillBuckets( rawData, filteringSettings );
	}

	void ResourceMetricsProcessor::Sort( ResourceMetrics::SortedBy by )
	{
		for ( Uint32 i = 0; i < m_buckets.Size(); ++i )
		{
			Sort( by, i );
		}
	}

	void ResourceMetricsProcessor::Sort( ResourceMetrics::SortedBy by, Uint32 bucketIndex )
	{
		for ( Uint32 i = 0; i < m_buckets[bucketIndex].subBuckets.Size(); ++i )
		{
			Sort( by, bucketIndex, i );
		}
	}

	void ResourceMetricsProcessor::Sort( ResourceMetrics::SortedBy by, Uint32 bucketIndex, Uint32 subBucketIndex )
	{
		auto& ar = m_buckets[bucketIndex].subBuckets[subBucketIndex].entries;

		m_buckets[bucketIndex].subBuckets[subBucketIndex].sortedBy = by;

		switch ( by )
		{
			case ResourceMetrics::SortedBy::NotSorted:
			{
				break;
			}
			case ResourceMetrics::SortedBy::GPU_MEMORY_INC:
			{
				std::stable_sort( ar.Begin(), ar.End(), [] ( const SingleResourceMetrics& first, const SingleResourceMetrics& second )
				{
					return first.gpuBytesUsed < second.gpuBytesUsed;
				} );
				break;
			}
			case ResourceMetrics::SortedBy::GPU_MEMORY_DEC:
			{
				std::stable_sort( ar.Begin(), ar.End(), [] ( const SingleResourceMetrics& first, const SingleResourceMetrics& second )
				{
					return first.gpuBytesUsed > second.gpuBytesUsed;
				} );
				break;
			}
			case ResourceMetrics::SortedBy::REF_COUNT_INC:
			{
				std::stable_sort( ar.Begin(), ar.End(), [] ( const SingleResourceMetrics& first, const SingleResourceMetrics& second )
				{
					return first.refCount < second.refCount;
				} );
				break;
			}
			case ResourceMetrics::SortedBy::REF_COUNT_DEC:
			{
				std::stable_sort( ar.Begin(), ar.End(), [] ( const SingleResourceMetrics& first, const SingleResourceMetrics& second )
				{
					return first.refCount > second.refCount;
				} );
				break;
			}
			case ResourceMetrics::SortedBy::RESOURCE_PATH_ALPHABETICALLY:
			{
				std::sort( ar.Begin(), ar.End(), [] ( const SingleResourceMetrics& first, const SingleResourceMetrics& second )
				{
					return strcmp( first.path.ToDebugString(), second.path.ToDebugString() ) < 0;
				} );
				break;
			}
			case ResourceMetrics::SortedBy::RESOURCE_PATH_INV_ALPHABETICALLY:
			{
				std::sort( ar.Begin(), ar.End(), [] ( const SingleResourceMetrics& first, const SingleResourceMetrics& second )
				{
					return strcmp( first.path.ToDebugString(), second.path.ToDebugString() ) > 0;
				} );
				break;
			}
			case ResourceMetrics::SortedBy::BigGPULowCount:
			{
				std::sort( ar.Begin(), ar.End(), [] ( const SingleResourceMetrics& first, const SingleResourceMetrics& second )
				{
					return first.refCount == second.refCount ? first.gpuBytesUsed > second.gpuBytesUsed : first.refCount < second.refCount;
				} );
				break;
			}

			default:
			{
				RED_FATAL( "Sorting algorithm not implemented." );
				break;
			}
		}
	}

	String ResourceMetricsProcessor::PrintTeamMemoryUsageToCSV() const
	{
		red::StringBuilder< String > builder;
		ForEachTeamUsage( [&builder] ( const String& teamName, Uint64 teamUsage, Uint64 teamLimit )
		{
			const Float usageInMB = (Float)teamUsage / (Float)RED_MEGA_BYTE( 1u );
			const Float limitPerc = (Float)teamUsage / (Float)teamLimit * 100.f;
			builder.Appendf( "%s;%.3f%%;%fMB\n", teamName.AsChar(), limitPerc, usageInMB );
		} );

		return builder.ToString();
	}

	String ResourceMetricsProcessor::PrintTotalMemoryUsageToCSV() const
	{
		red::StringBuilder< String > outputString;

		for ( const Bucket& bucket : m_buckets )
		{
			const Float megaBytes = (Float)bucket.usedGPUBytes / (Float)RED_MEGA_BYTE( 1 );
			outputString.Appendf( "%s;%.3f\n", bucket.name.AsChar(), megaBytes );

			for ( const SubBucket& subBucket : bucket.subBuckets )
			{
				const Float megaBytes = (Float)subBucket.usedGPUBytes / (Float)RED_MEGA_BYTE( 1 );
				outputString.Appendf( "  %s;%.3f\n", subBucket.name.AsChar(), megaBytes );
			}
		}

		return outputString.ToString();
	}

	namespace helper
	{
		const char* BoolToCString( Bool input )
		{
			return input ? "true" : "false";
		}

		const char* TextureGroupToCString( TextureGroup group )
		{
			switch ( group )
			{
				case res::TG_Unknown:
					return "Unknown";
				case res::TG_Generic:
					return "Generic";
				case res::TG_Multilayer:
					return "Multilayer";
				case res::TG_System:
					return "System";
				default:
					return "";
			}
		}

		static const Float s_base = 1.f / (Float)RED_MEGA_BYTE( 1u );

		void PrintMeshBucket( red::StringBuilder< String >& builder, const ResourceMetricsProcessor::Bucket& bucket )
		{
			builder.Appendf( "%s;path;GPU memory;ref count;in range;is skinned;has physics\n", bucket.name.AsChar() );

			for ( Uint32 i = 0; i < bucket.subBuckets.Size(); ++i )
			{
				const ResourceMetricsProcessor::SubBucket& subBucket = bucket.subBuckets[i];
				builder.Appendf( "  %s\n", subBucket.name.AsChar() );

				for ( Uint32 j = 0; j < subBucket.entries.Size(); ++j )
				{
					const SingleResourceMetrics& entry = subBucket.entries[j];

					const Float megabyteMem = (Float)entry.gpuBytesUsed * s_base;

					String bytesUsage = String::Printf( "%.4f", megabyteMem );

					builder.Appendf( ";%s;%s;%u;%u;%s;%s\n", entry.path.ToDebugString(), bytesUsage.AsChar(), entry.refCount, entry.mesh.inRange, BoolToCString( entry.mesh.isSkinned ), BoolToCString( entry.mesh.hasPhysics ) );
				}
			}
		}

		void PrintTextureBucket( red::StringBuilder< String >& builder, const ResourceMetricsProcessor::Bucket& bucket )
		{
			builder.Appendf( "%s;path;GPU memory;ref count;group\n", bucket.name.AsChar() );

			for ( Uint32 i = 0; i < bucket.subBuckets.Size(); ++i )
			{
				const ResourceMetricsProcessor::SubBucket& subBucket = bucket.subBuckets[i];
				builder.Appendf( "  %s\n", subBucket.name.AsChar() );

				for ( Uint32 j = 0; j < subBucket.entries.Size(); ++j )
				{
					const SingleResourceMetrics& entry = subBucket.entries[j];

					const Float megabyteMem = (Float)entry.gpuBytesUsed * s_base;

					String bytesUsage = String::Printf( "%.4f", megabyteMem );

					builder.Appendf( ";%s;%s;%u;%s\n", entry.path.ToDebugString(), bytesUsage.AsChar(), entry.refCount, TextureGroupToCString( entry.texture.group ) );
				}
			}
		}

		void PrintMaterialBucket( red::StringBuilder< String >& builder, const ResourceMetricsProcessor::Bucket& bucket )
		{
			builder.Appendf( "%s;path;GPU memory;ref count\n", bucket.name.AsChar() );

			for ( Uint32 i = 0; i < bucket.subBuckets.Size(); ++i )
			{
				const ResourceMetricsProcessor::SubBucket& subBucket = bucket.subBuckets[i];
				builder.Appendf( "  %s\n", subBucket.name.AsChar() );

				for ( Uint32 j = 0; j < subBucket.entries.Size(); ++j )
				{
					const SingleResourceMetrics& entry = subBucket.entries[j];

					const Float megabyteMem = (Float)entry.gpuBytesUsed * s_base;

					String bytesUsage = String::Printf( "%.4f", megabyteMem );

					builder.Appendf( ";%s;%s;%u\n", entry.path.ToDebugString(), bytesUsage.AsChar(), entry.refCount );
				}
			}
		}

		void PrintVideoBucket( red::StringBuilder< String >& builder, const ResourceMetricsProcessor::Bucket& bucket )
		{
			PrintMaterialBucket( builder, bucket );
		}

		void PrintCategoryDetailedToCSV( red::StringBuilder< String >& builder, const ResourceMetricsProcessor::Bucket& bucket )
		{
			if ( bucket.resType == ResourceMetricsType::Mesh )
				PrintMeshBucket( builder, bucket );
			else if ( bucket.resType == ResourceMetricsType::Texture )
				PrintTextureBucket( builder, bucket );
			else if ( bucket.resType == ResourceMetricsType::Material )
				PrintMaterialBucket( builder, bucket );
			else if ( bucket.resType == ResourceMetricsType::Video )
				PrintVideoBucket( builder, bucket );
		}
	}

	String ResourceMetricsProcessor::PrintCategoryDetailedToCSV( Uint32 index ) const
	{
		red::StringBuilder< String > builder;

		const Bucket& bucket = m_buckets[index];

		helper::PrintCategoryDetailedToCSV( builder, bucket );

		return builder.ToString();

	}

	String ResourceMetricsProcessor::PrintCategoriesDetailedToCSV( red::ArraySpan< Uint32 > indices ) const
	{
		red::StringBuilder< String > builder;

		for ( Uint32 index : indices )
		{
			const Bucket& bucket = m_buckets[index];
			helper::PrintCategoryDetailedToCSV( builder, bucket );
		}

		return builder.ToString();
	}

	String ResourceMetricsProcessor::PrintTeamMemoryUsageToJSON() const
	{
		String output;
		serialization::json::JsonTextWriter textWriter{ output };
		RapidJsonWriter writer{ textWriter };

		writer.StartArray();

		ForEachTeamUsage( [this, &writer] ( const String& teamName, Uint64 teamUsage, Uint64 teamLimit )
		{
			writer.StartObject();

			writer.Key( "teamName" );
			writer.String( teamName.AsChar() );

			writer.Key( "memoryUsageBytes" );
			writer.Uint64( teamUsage );

			writer.Key( "budgetBytes" );
			writer.Uint64( teamLimit );

			writer.Key( "percent" );
			writer.Double( ((Double)teamUsage / (Double)teamLimit * 100.) );

			writer.Key( "categories" );
			writer.StartArray();

			for ( auto& bucket : m_buckets )
			{
				if ( bucket.team == teamName )
				{
					writer.StartObject();

					writer.Key( "categoryName" );
					writer.String( bucket.name.AsChar() );

					writer.Key( "memoryUsageBytes" );
					writer.Uint64( bucket.usedGPUBytes );

					writer.Key( "budgetBytes" );
					writer.Uint64( bucket.limit );

					writer.Key( "subCategories" );
					writer.StartArray();

					for ( const auto& subBucket : bucket.subBuckets )
					{
						writer.StartObject();
						writer.Key( "subCategoryName" );
						writer.String( subBucket.name.AsChar() );

						writer.Key( "memoryUsageBytes" );
						writer.Uint64( subBucket.usedGPUBytes );

						writer.EndObject();
					}

					writer.EndArray();

					writer.EndObject();
				}
			}

			writer.EndArray();

			writer.EndObject();
		} );

		writer.EndArray();

		return output;
	}

	String ResourceMetricsProcessor::PrintTotalMemoryUsageToJSON() const
	{
		String output;
		serialization::json::JsonTextWriter textWriter{ output };
		RapidJsonWriter writer{ textWriter };

		writer.StartArray();

		for ( const auto& bucket : m_buckets )
		{
			writer.StartObject();

			writer.Key( "categoryName" );
			writer.String( bucket.name.AsChar() );

			writer.Key( "memoryUsage" );
			writer.Double( ((Double)bucket.usedGPUBytes / (Double)RED_MEGA_BYTE( 1u )) );

			writer.Key( "memoryUsageBytes" );
			writer.Uint64( bucket.usedGPUBytes );

			writer.Key( "percent" );
			writer.Double( ((Double)bucket.usedGPUBytes / (Double)bucket.limit * 100.) );

			writer.Key( "subCategories" );
			writer.StartArray();

			for ( const auto& subBucket : bucket.subBuckets )
			{
				writer.StartObject( );
				writer.Key( "subCategoryName" );
				writer.String( subBucket.name.AsChar() );

				writer.Key( "memoryUsage" );
				writer.Double( ((Double)subBucket.usedGPUBytes / (Double)RED_MEGA_BYTE( 1u )) );

				writer.Key( "memoryUsageBytes" );
				writer.Uint64( subBucket.usedGPUBytes );
				writer.EndObject( );
			}

			writer.EndArray();

			writer.EndObject();
		}

		writer.EndArray();

		return output;
	}

	namespace helper
	{
		void PrintMeshEntry( RapidJsonWriter& writer, const SingleResourceMetrics& entry )
		{
			writer.Key( "path" );
			writer.String( entry.path.ToDebugString() );

			writer.Key( "memoryUsage" );
			writer.String( red::FormatByteNumber( entry.gpuBytesUsed ).AsChar() );

			writer.Key( "refCount" );
			writer.Uint64( entry.refCount );

			writer.Key( "inRange" );
			writer.Uint64( entry.mesh.inRange );

			writer.Key( "hasPhysics" );
			writer.Bool( entry.mesh.hasPhysics );

			writer.Key( "isSkinned" );
			writer.Bool( entry.mesh.isSkinned );
		}

		void PrintTextureEntry( RapidJsonWriter& writer, const SingleResourceMetrics& entry )
		{
			writer.Key( "path" );
			writer.String( entry.path.ToDebugString() );

			writer.Key( "memoryUsage" );
			writer.String( red::FormatByteNumber( entry.gpuBytesUsed ).AsChar() );

			writer.Key( "refCount" );
			writer.Uint64( entry.refCount );

			writer.Key( "textureGroup" );
			writer.String( TextureGroupToCString( entry.texture.group ) );
		}

		void PrintMaterialEntry( RapidJsonWriter& writer, const SingleResourceMetrics& entry )
		{
			writer.Key( "path" );
			writer.String( entry.path.ToDebugString() );

			writer.Key( "memoryUsage" );
			writer.String( red::FormatByteNumber( entry.gpuBytesUsed ).AsChar() );

			writer.Key( "refCount" );
			writer.Uint64( entry.refCount );
		}
		void PrintVideoEntry( RapidJsonWriter& writer, const SingleResourceMetrics& entry )
		{
			PrintMaterialEntry( writer, entry );
		}


		void PrintEntry( RapidJsonWriter& writer, const SingleResourceMetrics& entry, ResourceMetricsType resType )
		{
			writer.StartObject();

			switch ( resType )
			{
				case ResourceMetricsType::Mesh:
				{
					PrintMeshEntry( writer, entry );
					break;
				}
				case ResourceMetricsType::Material:
				{
					PrintMaterialEntry( writer, entry );
					break;
				}
				case ResourceMetricsType::Texture:
				{
					PrintTextureEntry( writer, entry );
					break;
				}
				case ResourceMetricsType::Video:
				{
					PrintVideoEntry( writer, entry );
					break;
				}
				default:
					break;
			}
			

			writer.EndObject();
		}

		void PrintCategoryDetailedToJSON( RapidJsonWriter& writer, const ResourceMetricsProcessor::Bucket& bucket )
		{
			writer.StartObject();
			writer.Key( "categoryName" );
			writer.String( bucket.name.AsChar() );

			writer.Key( "memoryUsage" );
			writer.String( red::FormatByteNumber( bucket.usedGPUBytes ).AsChar() );

			writer.Key( "percent" );
			writer.Double( ((Double)bucket.usedGPUBytes / (Double)bucket.limit * 100.) );

			writer.Key( "subCategories" );
			writer.StartArray();

			for ( Uint32 i = 0u; i < bucket.subBuckets.Size(); ++i )
			{
				const ResourceMetricsProcessor::SubBucket& subBucket = bucket.subBuckets[i];

				writer.StartObject();

				writer.Key( "name" );
				writer.String( subBucket.name.AsChar() );

				writer.Key( "memoryUsage" );
				writer.String( red::FormatByteNumber( subBucket.usedGPUBytes ).AsChar() );

				writer.Key( "entries" );

				writer.StartArray();
				for ( Uint32 j = 0; j < subBucket.entries.Size(); ++j )
				{
					PrintEntry( writer, subBucket.entries[j], bucket.resType );
				}
				writer.EndArray();

				writer.EndObject();
			}

			writer.EndArray();

			writer.EndObject();
		}
	}

	String ResourceMetricsProcessor::PrintCategoryDetailedToJSON( Uint32 index ) const
	{
		String output;
		serialization::json::JsonTextWriter textWriter{ output };
		RapidJsonWriter writer{ textWriter };

		const Bucket& bucket = m_buckets[index];

		helper::PrintCategoryDetailedToJSON( writer, bucket );

		return output;
	}

	String ResourceMetricsProcessor::PrintCategoriesDetailedToJSON( red::ArraySpan< Uint32 > indices ) const
	{
		String output;
		serialization::json::JsonTextWriter textWriter{ output };
		RapidJsonWriter writer{ textWriter };

		writer.StartArray();
		for ( Uint32 index : indices )
		{
			const Bucket& bucket = m_buckets[index];
			helper::PrintCategoryDetailedToJSON( writer, bucket );

		}
		writer.EndArray();

		return output;
	}

	void ResourceMetricsProcessor::InitBucketsFromSettings( const ResourceMetrics::Settings& filteringSettings )
	{
		if ( m_cache )
			m_cache->Clear();

		m_buckets.Clear();
		m_buckets.Reserve( filteringSettings.categories.Size() );

		for ( const ResourceMetrics::Settings::Category& category : filteringSettings.categories )
		{
			auto& newBucket = m_buckets.EmplaceBack();
			newBucket.name = category.name;
			newBucket.team = category.team;
			newBucket.limit = category.limit;
			newBucket.resType = category.resType;

			newBucket.subBuckets.Reserve( category.subCategories.Size() );

			for ( const ResourceMetrics::Settings::SubCategory& subCategory : category.subCategories )
			{
				auto& newSubCategory = newBucket.subBuckets.EmplaceBack();
				newSubCategory.name = subCategory.name;
			}

			m_teams[category.team].gpuBytesLimit += category.limit;
		}
	}

	namespace helper
	{
		static Bool TryFilter( const ResourceMetrics::Settings::PathFilter& pathFilter, const res::ResourcePath& resPath, int& matchLevel )
		{
			red::StringView view{ resPath.ToStringView() };

			if ( pathFilter.resourcePath1[0] != '\0' )
			{
				if ( view.Find( pathFilter.resourcePath1 ) == red::StringView::npos )
				{
					return false;
				}
				++matchLevel;
			}

			if ( pathFilter.resourcePath2[0] != '\0' )
			{
				if ( view.Find( pathFilter.resourcePath2 ) == red::StringView::npos )
				{
					return false;
				}
				++matchLevel;
			}

			return true;
		}
	}

	void ResourceMetricsProcessor::ProcessRawDataAndFillBuckets( RawResourceMetrics& rawData, const ResourceMetrics::Settings& filteringSettings )
	{
		//Clear old data
		for( Uint32 bucketIndex = 0; bucketIndex < m_buckets.Size( ); ++bucketIndex )
		{
			Bucket& currBucket = m_buckets[ bucketIndex ];
			currBucket.usedGPUBytes = 0u;
			for( Uint32 subBucketIndex = 0u; subBucketIndex < currBucket.subBuckets.Size( ); ++subBucketIndex )
			{
				SubBucket& currSubBucket = currBucket.subBuckets[ subBucketIndex ];
				currSubBucket.usedGPUBytes = 0u;
				currSubBucket.entries.Clear( );
			}
		}
		for( auto team : m_teams )
		{
			team.Value( ).gpuBytesUsed = 0u;
		}

		ProcessRawDataType( res::ResourceMetricsType::Material, rawData.materials,  filteringSettings );
		ProcessRawDataType( res::ResourceMetricsType::Mesh, rawData.meshes, filteringSettings );
		ProcessRawDataType( res::ResourceMetricsType::Texture, rawData.textures, filteringSettings );
		ProcessRawDataType( res::ResourceMetricsType::Video, rawData.videos, filteringSettings );

		for ( Uint32 i = 0; i < m_buckets.Size(); ++i )
		{
			auto& bucket = m_buckets[i];

			for ( Uint32 j = 0; j < bucket.subBuckets.Size(); ++j )
			{
				auto& subBucket = bucket.subBuckets[j];
				if ( subBucket.sortedBy != ResourceMetrics::NotSorted )
				{
					Sort( subBucket.sortedBy, i, j );
				}
			}
		}
	}

	void ResourceMetricsProcessor::ProcessRawDataType( res::ResourceMetricsType resType, res::RawResourceMetrics::ResType& data, const ResourceMetrics::Settings& filteringSettings )
	{
		//Process new data
		//match data to BEST bucket - highest match level or first (in case of equal match levels)
		for( Uint32 rawEntryIndex = 0u; rawEntryIndex < data.metrics.Size( ); ++rawEntryIndex )
		{
			SingleResourceMetrics& entry = data.metrics[ rawEntryIndex ];

			if ( m_cache )
			{
				auto cachedPos = m_cache->Find( entry.path );
				if ( cachedPos.HasValue() )
				{
					Bucket& b = m_buckets[std::get< 0 >( cachedPos.Value() )];
					SubBucket& sb = b.subBuckets[std::get< 1 >( cachedPos.Value() )];
					String& teamName = std::get< 2 >( cachedPos.Value() );

					PushToSubBucket( sb, entry );
					b.usedGPUBytes += entry.gpuBytesUsed;
					m_teams[ teamName ].gpuBytesUsed += entry.gpuBytesUsed;
					continue;
				}
			}

			Int32 bestMatchingLevel = -1;
			Bucket* bestMatchingBucket = nullptr;
			SubBucket* bestMatchingSubBucket = nullptr;
			Uint32 bestMatchingBucketIdx = 0xFFFFFFFF;
			Uint32 bestMatchingSubBucketIdx = 0xFFFFFFFF;

			red::String bestMatchingTeam;

			for( Uint32 bucketIndex = 0; bucketIndex < m_buckets.Size( ); ++bucketIndex )
			{
				Bucket& currBucket = m_buckets[ bucketIndex ];
				const ResourceMetrics::Settings::Category& currCategory = filteringSettings.categories[ bucketIndex ];

				//basic 'data type' vs 'bucket type' check
				if( currCategory.resType != resType )
					continue;

				Int32 matchingLevel = 0;
				if( !helper::TryFilter( currCategory.filter, entry.path, matchingLevel ) )
					continue;

				SubBucket* defaultBucket = nullptr;
				SubBucket* matchingSubBucket = nullptr;

				Uint32 matchingSubBucketIdx = 0xFFFFFFFF;

				for( Uint32 subBucketIndex = 0u; subBucketIndex < currBucket.subBuckets.Size( ); ++subBucketIndex )
				{
					SubBucket& currSubBucket = currBucket.subBuckets[ subBucketIndex ];
					const ResourceMetrics::Settings::SubCategory& currSubCategory = currCategory.subCategories[ subBucketIndex ];

					if( currSubBucket.name == "Default" )
					{
						defaultBucket = &currSubBucket;
						matchingSubBucketIdx = subBucketIndex;
						continue;
					}
					else
					{
						if( helper::TryFilter( currSubCategory.filter, entry.path, matchingLevel ) )
						{
							matchingSubBucket = &currSubBucket;
							matchingSubBucketIdx = subBucketIndex;
							break;
						}
					}
				}

				if( !matchingSubBucket )
				{
					RED_FATAL_ASSERT( defaultBucket, "Default subcategory missing" );
					matchingSubBucket = defaultBucket;
				}

				if( matchingLevel > bestMatchingLevel )
				{
					bestMatchingTeam = currCategory.team;
					bestMatchingLevel = matchingLevel;
					bestMatchingBucket = &currBucket;
					bestMatchingSubBucket = matchingSubBucket;
					bestMatchingBucketIdx = bucketIndex;
					bestMatchingSubBucketIdx = matchingSubBucketIdx;

					if ( matchingLevel == 4 )
						break; //No better match can be found.
				}
			}

			RED_FATAL_ASSERT( bestMatchingSubBucket, "Failed to find any bucket for this asset!" );
#if 0
			RED_LOG_INFO(
				"Pushing [%hs] into bucket [%hs] sub [%hs]",
				entry.path.ToDebugString( ),
				bestMatchingBucket->name.AsChar( ),
				bestMatchingSubBucket->name.AsChar( )
			);
#endif
			PushToSubBucket( *bestMatchingSubBucket, entry );
			bestMatchingBucket->usedGPUBytes += entry.gpuBytesUsed;
			m_teams[ bestMatchingTeam ].gpuBytesUsed += entry.gpuBytesUsed;

			if ( m_cache )
			{
				m_cache->Add( entry.path, bestMatchingBucketIdx, bestMatchingSubBucketIdx, bestMatchingTeam );
			}
		}
	}

	void ResourceMetricsProcessor::PushToSubBucket( SubBucket& to, const SingleResourceMetrics& entry )
	{
		to.usedGPUBytes += entry.gpuBytesUsed;
		to.entries.PushBack( entry );
	}
}
