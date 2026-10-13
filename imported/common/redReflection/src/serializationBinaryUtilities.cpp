/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "serializationBinaryUtilities.h"
#include "resourceToken.h"
#include "serializationBinaryLoader.h"
#include "serializationBinaryRuntimeTables.h"
#include "serializationDecompressor.h"

#include "../../redCore/include/absolutePath.h"
#include "../../redFileSystem/include/memoryFileReader.h"

namespace serialization
{
	namespace prv
	{
		Bool Helper_ReadHeader( serialization::IAsyncSource& source, FileTables::Header& inOutHeader )
		{
			if ( source.GetSizeOnDisk() < sizeof( inOutHeader ) )
			{
				RED_LOG_WARNING( "Serialization: File '%hs' is corrupted", source.Debug_GetResourcePath().ToDebugString() );
				return false;
			}

			// load the header, we don't have to load everything now
			red::Memzero( &inOutHeader, sizeof( inOutHeader ) );
			if ( !source.ReadInline( 0, sizeof( inOutHeader ), red::BlobSpan::OfItem( inOutHeader ), io::eAsyncPriority_High, io::RequestSource::Tools, nullptr ) )
			{
				RED_LOG_WARNING( "Serialization: File '%hs' does not contain enough data for the header", source.Debug_GetResourcePath().ToDebugString() );
				return false;
			}

			// validate header
			if ( inOutHeader.m_magic != FileTables::FILE_MAGIC )
			{
				RED_LOG_WARNING( "Serialization: File '%hs' is not a resource file", source.Debug_GetResourcePath().ToDebugString() );
				return false;
			}

			// All good so far
			return true;
		}

		Uint32 GetPayloadSize( serialization::IAsyncSource& source )
		{
			FileTables::Header header;
			if ( Helper_ReadHeader( source, header ) )
			{
				// All good
				return header.m_objectsEnd;
			}
			// Smth wrong with the header
			return 0;
		}
	} // prv

	namespace tools
	{
		Uint32 GetGPUSize( IAsyncSource& source )
		{
			FileTables::Header header;
			if ( prv::Helper_ReadHeader( source, header ) )
			{
				// All good
				return header.m_gpuSize;
			}
			// Smth wrong with the header
			return 0;
		}

		THandle< ISerializable > ExtractSingleObject( serialization::IAsyncSource& source )
		{
			Uint32 payloadSize = prv::GetPayloadSize( source );
			if ( payloadSize == 0 )
			{
				return nullptr;
			}

			// load the stuff
			red::DynArray< Uint8 > payload{ payloadSize, red::PoolBackend() };
			source.ReadInline( 0, payloadSize, red::MakeBlobSpan( payload ), io::eAsyncPriority_High, io::RequestSource::Tools, nullptr );

			// load the file tables
			FileTables tables;
			CMemoryFileReader reader( payload.TypedData(), payload.Size(), 0 );
			Uint32 fileVersion = 0;
			if ( !tables.Load( reader, fileVersion ) )
			{
				RED_LOG_WARNING( "Serialization: Resource file '%hs' does not contain valid data tables", source.Debug_GetResourcePath().ToDebugString() );
				return nullptr;
			}

			LoadingResult result;

			// resolve the tables, do not load any resources
			{
				RuntimeTables runtimeTables{ RuntimeTables::eNoLoading };
				LoadingContext context;
				context.m_skipPostLoad = true;
				context.m_skipImports = true;
				context.m_skipBuffers = true;
				runtimeTables.Resolve( context, tables );

				// build data
				runtimeTables.CreateExports( context, tables );

				// Unused since no asyncSource 
				job::Counter nullCounter;

				// load data
				reader.m_version = fileVersion;
				runtimeTables.LoadExports( 0, reader, nullCounter, context, tables, result );
			}

			RED_FATAL_ASSERT( !result.m_loadedRootObjects.Empty(), "Expected at least one root object" );
			return result.m_loadedRootObjects.Front();
		}	

		Bool CollectImports(IAsyncSource& source, red::DynArray<res::ResourcePath>& outImports)
		{
			Uint32 payloadSize = prv::GetPayloadSize(source);
			if (payloadSize == 0)
			{
				return false;
			}

			// load the stuff
			red::DynArray< Uint8 > payload{ payloadSize, red::PoolBackend() };
			source.ReadInline(0, payloadSize, red::MakeBlobSpan( payload ), io::eAsyncPriority_High, io::RequestSource::Tools, nullptr );

			// load the file tables
			FileTables fileTables;
			CMemoryFileReader reader(payload.TypedData(), payload.Size(), 0);
			Uint32 fileVersion = 0;
			if (!fileTables.Load(reader, fileVersion))
			{
				RED_LOG_WARNING("Serialization: Resource file '%hs' does not contain valid data tables", source.Debug_GetResourcePath().ToDebugString());
				return false;
			}

			for (Uint32 i : fileTables.m_imports.Indices())
			{
				const auto& data = fileTables.m_imports[i];

				// resolve path
				const AnsiChar* string = &fileTables.m_strings[data.m_path];
				outImports.PushBack(res::ResourcePath::Build(string));
			}
		
			return true;
		}

		Bool CollectInplaceResources(IAsyncSource& source, red::DynArray<res::ResourcePath>& outInplaceResources)
		{
			Uint32 payloadSize = prv::GetPayloadSize(source);
			if (payloadSize == 0)
			{
				return false;
			}

			// load the stuff
			red::DynArray< Uint8 > payload{ payloadSize, red::PoolBackend() };
			source.ReadInline(0, payloadSize, red::MakeBlobSpan(payload), io::eAsyncPriority_High, io::RequestSource::Tools, nullptr);

			// load the file tables
			FileTables fileTables;
			CMemoryFileReader reader(payload.TypedData(), payload.Size(), 0);
			Uint32 fileVersion = 0;
			if (!fileTables.Load(reader, fileVersion))
			{
				RED_LOG_WARNING("Serialization: Resource file '%hs' does not contain valid data tables", source.Debug_GetResourcePath().ToDebugString());
				return false;
			}

			for (Uint32 i : fileTables.m_inplace.Indices())
			{
				const FileTables::InplaceResource& inplaceResouce = fileTables.m_inplace[i];
				
				// NULL resource
				if (inplaceResouce.m_importIndex == 0)
				{
					continue;
				}

				const Uint32 importIndex = inplaceResouce.m_importIndex - 1U;

#ifndef RED_CONFIGURATION_FINAL
				if (importIndex >= fileTables.m_imports.Size())
				{
					continue;
				}
#endif

				const auto& data = fileTables.m_imports[importIndex];

				// resolve path
				const AnsiChar* string = &fileTables.m_strings[data.m_path];
				auto path = res::ResourcePath::Build(string);
				if (path.IsValid())
				{
					outInplaceResources.PushBack(path);
				}
			}

			return true;
		}

	}

	namespace prv
	{
		RuntimeTables::BufferIndex FindMetadataBufferIndex( IAsyncSource& source, const FileTables& tables )
		{
			Uint16 metadataNameIndex = 0; // 0 is always the "null" string / name
			for ( Uint32 index = 0; index != tables.m_names.Size(); ++index )
			{
				if ( red::StrcmpNC( &tables.m_strings[ tables.m_names[ index ].m_string ], "metadata" ) == 0 )
				{
					metadataNameIndex = static_cast<Uint16>( index );
					break;
				}
			}
			if ( metadataNameIndex == 0 )
			{
				return 0;
			}


			// Parse the object and find the metadata member in the file, assuming it's a buffer inline
			red::DynArray< Uint8 > firstExportBuffer{ red::PoolBackend() };
			firstExportBuffer.Resize( tables.m_exports[0].m_dataSize );
			source.ReadInline( tables.m_exports[0].m_dataOffset, firstExportBuffer.Size(), red::MakeBlobSpan( firstExportBuffer ), io::eAsyncPriority_High, io::RequestSource::Tools, nullptr );
			CMemoryFileReader reader( firstExportBuffer.TypedData(), firstExportBuffer.Size(), 0 );

			{
				Uint8 flags = 0;
				reader << flags;
			}

			// Read properties list
			for ( ; ; )
			{
				Uint16 nameIndex = 0;
				reader << nameIndex;

				if ( nameIndex == 0 )
				{
					break;
				}

				Uint16 typeIndex = 0;
				reader << typeIndex;
				Uint32 skipDistance = 0;
				reader << skipDistance;

				if ( nameIndex == metadataNameIndex )
				{
					Uint16 bufferIndex = 0;
					reader << bufferIndex;
					return bufferIndex;
				}

				// Skip distance in file includes itself
				skipDistance -= sizeof(skipDistance);
				reader.Seek( reader.GetOffset() + skipDistance );
			}

			return 0;
		}
	}

	BufferHandleRO LoadMetadataBuffer( IAsyncSource& source )
	{
		// load the header, we don't have to load everything now
		FileTables::Header header;
		if ( source.GetSizeOnDisk() < sizeof( header ) || !source.ReadInline( 0, sizeof( header ), red::BlobSpan::OfItem( header ), io::eAsyncPriority_High, io::RequestSource::Tools, nullptr ) )
		{
			RED_LOG_WARNING( "Serialization: File '%s' does not contain enough data for the header",
				source.Debug_GetResourcePath().ToDebugString() );
			return nullptr;
		}

		// validate header
		if ( header.m_magic != FileTables::FILE_MAGIC )
		{
			RED_LOG_WARNING( "Serialization: File '%s' is not a resource file",
				source.Debug_GetResourcePath().ToDebugString());
			return nullptr;
		}

		// determine the size of the object payload
		const auto payloadSize = header.m_objectsEnd;
		const auto maxPayloadSize = 10 * 1024 * 1024;
		if ( payloadSize > maxPayloadSize )
		{
			RED_LOG_WARNING( "Serialization: Resource file '%s is too big for direct loading (%1.2f MB)",
				source.Debug_GetResourcePath().ToDebugString(), payloadSize / static_cast<float>( RED_MEGA_BYTE( 1 ) ) );
			return nullptr;
		}

		// load objects
		red::DynArray< Uint8 > payload{ payloadSize, red::PoolBackend() };
		source.ReadInline( 0, payloadSize, red::MakeBlobSpan( payload ), io::eAsyncPriority_High, io::RequestSource::Tools, nullptr );

		// load the file tables
		FileTables tables;
		CMemoryFileReader reader( payload.TypedData(), payload.Size(), 0 );
		Uint32 fileVersion = 0;
		if ( !tables.Load( reader, fileVersion ) )
		{
			RED_LOG_WARNING( "Serialization: Resource file '%s' does not contain valid data tables",
				source.Debug_GetResourcePath().ToDebugString());
			return nullptr;
		}

		// resolve the tables, do not load any resources
		RuntimeTables runtimeTables{ RuntimeTables::eNoLoading };
		LoadingContext context;

		context.m_skipImports = true;
		context.m_skipPostLoad = true;

		runtimeTables.Resolve( context, tables );

		if ( runtimeTables.m_mappedBuffers.Empty() )
		{
			return nullptr;
		}

		auto bufferIndex = prv::FindMetadataBufferIndex( source, tables );
		if ( bufferIndex == 0 )
		{
			return nullptr;
		}

		// load buffer
		const RuntimeTables::ResolvedBuffers& buffInfo = runtimeTables.m_mappedBuffers[ bufferIndex - 1 ];

		auto readBuf = red::CreateUniqueBuffer< red::PoolEngine >( buffInfo.m_dataSizeOnDisk, DeferredDataBuffer::c_defaultAlignment );
		if( !source.ReadInline( buffInfo.m_dataOffset, buffInfo.m_dataSizeOnDisk, red::MakeBlobSpan( readBuf ), io::eAsyncPriority_High, io::RequestSource::Tools, nullptr ) )
		{
			RED_LOG_WARNING( "Serialization: Failed to read metadata buffer from '%s'",
				source.Debug_GetResourcePath().ToDebugString());
			return nullptr;
		}

		if ( buffInfo.m_dataSizeInMemory != buffInfo.m_dataSizeOnDisk )
		{
			auto decompressedBuffer = red::CreateUniqueBuffer< red::PoolEngine >( buffInfo.m_dataSizeInMemory, DeferredDataBuffer::c_defaultAlignment );
			RED_FATAL_ASSERT( readBuf.Get() );

			const String debugLogicalFileName = red::paths::ExtractFileStem( source.Debug_GetResourcePath().ToStringView() );
			if( !Decompressor::DecompressData( debugLogicalFileName.AsChar(), red::MakeBlobView( readBuf ), red::MakeBlobSpan( decompressedBuffer ) ) )
			{
				RED_LOG_WARNING( "Serialization: Failed to decompress metadata buffer from '%s'",
					source.Debug_GetResourcePath().ToDebugString() );
				return nullptr;
			}

			return BufferHandleRO( std::move( decompressedBuffer ) );
		}

		return BufferHandleRO( std::move( readBuf ) );
	}

} // serialization
