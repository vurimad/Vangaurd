/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "serializationMapping.h"
#include "serializationSaver.h"
#include "serializationBinarySaver.h"
#include "serializationBinaryStructureMapper.h"
#include "serializationFileTables.h"
#include "serializationFileTablesBuilder.h"

#include "../../redFileSystem/include/file.h"
#include "../../redSystem/include/crc.h"
#include "../../redCore/include/commandline.h"
#include "../../redJobs2/include/jobBuilder.h"

namespace dd
{
	static red::CrashDataThreadLocal< String, 260 > compressingFile{ "Debug", "CompressingFile" };
}


namespace serialization
{

	struct ProcessedBufferInfo
	{
		const void* data;
		Uint32 diskSize;
		Uint32 crc;
		compression::ResultBufferPtr pinnedBuffer;
	};

	static bool ProcessBuffer_NoLock( const IFile& file, const SavingContext& context, const StructureMapper::BufferInfo& mappedBuffer, ProcessedBufferInfo& processedBuffer );
	static bool WriteBuffers_NoLock( IFile& file, const SavingContext& context, const StructureMapper& mapper, const red::DynArray< ProcessedBufferInfo >& processedBuffers, FileTablesBuilder& tables, Uint64 headerOffset );


	BinarySaver::BinarySaver() = default;

	Bool BinarySaver::SaveObjects( IFile& file, const SavingContext& context ) const
	{
		// analyze structure of objects to save
		StructureMapper structureMapper;
		structureMapper.MapObjects( context );

		if ( structureMapper.m_names.Size() >= static_cast<Uint32>(std::numeric_limits<IMapper::NameIndex>::max() - 2) )
		{
			RED_LOG_ERROR( "BinarySaver: Too many names (%u) in resource '%hs'", structureMapper.m_names.Size(), file.GetFileNameForDebug() );
			return false;
		}
		if ( structureMapper.m_imports.Size() >= static_cast<Uint32>(std::numeric_limits<IMapper::PathIndex>::max() - 2) )
		{
			RED_LOG_ERROR( "BinarySaver: Too many imports (%u) in resource '%hs'", structureMapper.m_imports.Size(), file.GetFileNameForDebug() );
			return false;
		}

		// build serialization tables using the captured data
		FileTables fileTables;
		FileTablesBuilder tableBuilder( fileTables, structureMapper );
		{
			SetupNames( structureMapper, tableBuilder );
			SetupImports( structureMapper, tableBuilder );
			SetupExports( structureMapper, tableBuilder );
			SetupBuffers( structureMapper, tableBuilder );
			SetupInplaceResources( structureMapper, tableBuilder );
		}

		// Store the GPU video memory cost			
		fileTables.m_gpuSize = context.m_gpuSize;

		// Write the initial header, all offsets are RELATIVE to the header, not to the file start
		const Uint64 headerOffset = file.GetOffset();
		fileTables.Save( file, headerOffset );

		// Write exports
		if ( !WriteExports( file, context, structureMapper, tableBuilder, headerOffset ) )
		{
			return false;
		}

		// Write buffers, NOTE: buffers can be extracted here
		if ( !WriteBuffers( file, context, structureMapper, tableBuilder, headerOffset ) )
		{
			return false;
		}

		// Resave header, now with full set of data
		const Uint64 endOffset = file.GetOffset();
		file.Seek( headerOffset );
		fileTables.Save( file, headerOffset );
		file.Seek( endOffset );

		// objects saved
		return true;
	}

	void BinarySaver::SetupNames( StructureMapper& mapper, FileTablesBuilder& builder )
	{
		for ( Uint32 i=1; i<mapper.m_names.Size(); ++i )
		{
			const auto& name = mapper.m_names[i];
			builder.AddName( name );
		}
	}

	void BinarySaver::SetupImports( StructureMapper& mapper, FileTablesBuilder& builder )
	{
		// Emit imports into the file tables
		for ( const auto& import : mapper.m_imports )
		{
			RED_FATAL_ASSERT( import.m_path.IsValid(), "Import with no resource" );

			FileTablesBuilder::ImportInfo importBuilder;
			importBuilder.m_isSoft = import.m_soft;
			importBuilder.m_isObligatory = false;
			importBuilder.m_path = import.m_path;

			builder.AddImport( importBuilder );
		}
	}

	void BinarySaver::SetupInplaceResources( StructureMapper& mapper, FileTablesBuilder& builder )
	{
		for( const auto& inplaceResource : mapper.m_inplaceResources )	
		{
			FileTablesBuilder::InplaceResourceInfo info;
			info.m_importIndex = inplaceResource.importIndex;
			info.m_exportIndex = inplaceResource.exportIndex;

			builder.AddInplaceResource( info );
		}
	}

	void BinarySaver::SetupExports( StructureMapper& mapper, FileTablesBuilder& builder )
	{
		// Emit export data
		for ( const auto& exp : mapper.m_exports )
		{
			// setup export info
			FileTablesBuilder::ExportInfo exportBuilder;
			exportBuilder.m_className = exp->GetClass()->GetName();

			// ISerializable
			if ( exp )
			{
				// Map parent
				Int32 parent = 0;
				if ( exp->GetParent() )
				{
					if( !mapper.IsCloner() && !mapper.IsCooker() )
					{
						if( mapper.FindObjectIndex( exp->GetParent(), parent ) )
						{
							const auto index = &exp - mapper.m_exports.TypedData();
							RED_FATAL_ASSERT( parent != 0, "Non-NULL parent mapped to NULL" );

							const auto& parentInfo = mapper.m_exports[ parent - 1 ];
							RED_FATAL_ASSERT( parent <= index, "Parent (%d) '%hs' coming after child (%d) '%hs'",
											  parent, parentInfo->GetClass()->GetName().AsChar(),
											  index, exp->GetClass()->GetName().AsChar() );
						}
					}
					else if( mapper.IsCooker() )
					{
						const ISerializable* parentPtr = exp->GetParent();
						while( parentPtr != nullptr )
						{
							if( parentPtr->IsA< CResource >() )
							{
								if( mapper.FindObjectIndex( parentPtr, parent ) )
								{
									// ctremblay: set parent ONLY if inplace resource is parent.
									// Else every single resource will be invalidated, resulting in a full patch.
									auto iter = std::find_if( mapper.m_inplaceResources.Begin(), mapper.m_inplaceResources.End(), 
										[parent]( const StructureMapper::InplaceResourceInfo& info ) { return info.exportIndex == parent - 1; } );
									
									if( iter == mapper.m_inplaceResources.End() )
									{
										parent = 0;
									}
								}

								break;
							}

							parentPtr = parentPtr->GetParent();
						}
					}
				}

				exportBuilder.m_parent = parent;
			}

			// Register in output data, for now we do not support re indexing
			builder.AddExport( exportBuilder );
		}
	}

	void BinarySaver::SetupBuffers( StructureMapper& mapper, FileTablesBuilder& builder )
	{
		// Emit buffer data in same order as mapper
		for ( const auto& buf : mapper.m_buffers )
		{
			// extra paranoia
			RED_FATAL_ASSERT( buf.m_key != 0, "Found mapped buffer with invalid ID" );
			RED_FATAL_ASSERT( buf.m_sizeInMemoryDuringStructureMap != 0, "Found mapped buffer with zero size" );
			RED_FATAL_ASSERT( buf.m_inMemoryBuffer.Data() != nullptr, "Defered data buffer got lost before it got saved. File cannot be saved like that. Check your save related logic." );
			
			// E.g., check that didn't clear out buffer after structure map, since will have multiple serialize writer calls.
			RED_FATAL_ASSERT( buf.m_inMemoryBuffer.GetSize() == buf.m_sizeInMemoryDuringStructureMap, "Defered data inmemory buffer's size has changed (%d->%d) before it got saved. File cannot be saved like that. Check your save related logic.", buf.m_sizeInMemoryDuringStructureMap, buf.m_inMemoryBuffer.GetSize() );

			RED_FATAL_ASSERT(buf.m_precompressedBufferForCooking.GetSize() == buf.m_sizePrecompressedForCookingDuringStructureMap, "Defered data ondisk buffer's size has changed (%d->%d) before it got saved. File cannot be saved like that. Check your save related logic.", buf.m_sizeInMemoryDuringStructureMap, buf.m_precompressedBufferForCooking.GetSize());

			// Setup buffer info and add it to the builder
			// Sizes will be patched after writing the buffers out
			FileTablesBuilder::BufferInfo bufferBuilder;
			bufferBuilder.m_dataSizeInMemory = buf.m_sizeInMemoryDuringStructureMap; // the only thing we know for sure right now is the size in memory :)
			bufferBuilder.m_dataSizeOnDisk = 0; // even with a precompressed buffer, we might not use it if its compression setting is out of date

			bufferBuilder.m_hintGPUMemory = buf.m_hintGPUMemory;

			bufferBuilder.m_hintAutoLoadPC = buf.m_hintAutoLoadPC;
			bufferBuilder.m_hintAutoLoadXboxOne = buf.m_hintAutoLoadXboxOne;
			bufferBuilder.m_hintAutoLoadPS4 = buf.m_hintAutoLoadPS4;

			// add to buffer table
			builder.AddBuffer( bufferBuilder );
		}
	}

	namespace Helper
	{
		// mapping used to translate data to already preallocated indices
		class SavingMapper : public IMapper, public red::NonCopyable
		{
		public:
			SavingMapper( const StructureMapper& structureMapper, const FileTablesBuilder& fileTables )
				: m_structureMapper( structureMapper )
				, m_fileTables( fileTables )
			{}

			virtual void MapName( const CName& name, NameIndex& outIndex ) override final
			{
				outIndex = m_fileTables.MapName(name);
			}

			virtual void MapType( const rtti::IType* rttiType, TypeIndex& outIndex ) override final
			{
				outIndex = rttiType ? m_fileTables.MapName( rttiType->GetName() ) : 0;
			}

			virtual void MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex ) override final
			{
				Int32 index = 0;
				if ( objectRef )
				{
					m_structureMapper.FindObjectIndex( objectRef.Get(), index );
				}
				outIndex = static_cast< ObjectIndex >( index );
			}

			virtual void MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex ) override final
			{
				Int32 index = 0;
				if ( path.IsValid() )
				{
					m_structureMapper.FindImportIndex( path, index );
				}
				outIndex = static_cast< PathIndex >( index );
			}

			virtual void MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex ) override final
			{
				MapResourceReference( path, outIndex );
			}

			virtual void MapBuffer( const MapBufferParam& param, BufferIndex& outIndex ) override final
			{
				Int32 index = 0;
				if ( param.inMemoryData.Data() )
				{
					// calculate data CRC - it's the KEY 
					const Uint32 crc = red::CalculateCRC32( param.inMemoryData.Data(), param.inMemoryData.GetSize() );
					RED_FATAL_ASSERT( crc != 0, "Buffer has invalid CRC and it's NOT going to be saved. Serious bug that cannot be ignored." );
					if ( !m_structureMapper.FindBufferIndex( crc, index ) )
					{
						RED_FATAL( "Trying to save unmapped deferred data buffer. All new content should be generated in OnPreSave( const PreSaveContext& context )." );
					}
				}
				outIndex = static_cast< BufferIndex >( index );
			}

			/// NOT USED
			virtual void UnmapName( const NameIndex index, CName& outName ) override final { RED_FATAL("Unmapping is not part of the interface"); };
			virtual void UnmapType( const TypeIndex index,const rtti::IType*& outTypeRef ) override final { RED_FATAL("Unmapping is not part of the interface"); };
			virtual void UnmapPointer( const ObjectIndex index, THandle< ISerializable >& outObjectRef ) override final { RED_FATAL("Unmapping is not part of the interface"); };
			virtual void UnmapResourceReference( const PathIndex index, res::ResourcePath& outPath, res::ResourceTokenHandle & outToken ) override final { RED_FATAL("Unmapping is not part of the interface"); };
			virtual void UnmapResourceDeferredReference( const PathIndex index, res::ResourcePath& outPath ) override final { RED_FATAL("Unmapping is not part of the interface"); };
			virtual void UnmapBuffer( const BufferIndex index, const LoadBufferToken& token, BufferAsyncPtr& outBufferAccess ) override final { RED_FATAL("Unmapping is not part of the interface"); };

			virtual bool IsRelinking() const override final { return false; }
			virtual res::ResourcePath RelinkPath( const res::ResourcePath& path ) const override final { return path; }

		private:
			const StructureMapper&		m_structureMapper;
			const FileTablesBuilder&	m_fileTables;
		};
	}

	Bool BinarySaver::WriteExports( IFile& file, const SavingContext& context, const StructureMapper& mapper, FileTablesBuilder& tables, const Uint64 headerOffset ) const
	{
		// bind mapper to file because we want to use indexed tables
		Helper::SavingMapper savingMapper( mapper, tables ); 
		file.m_mapper = &savingMapper;

		for ( Uint32 index = 0; index<mapper.m_exports.Size(); ++index )
		{
			const auto& exp = mapper.m_exports[index];

			// Get export offset
			const auto exportDataOffset = static_cast< Uint32 >( file.GetOffset() - headerOffset );

			// Save using the serialization interface
			exp->OnSerialize( file );

			// Calculate data size
			const auto exportDataSize = static_cast< Uint32 >( file.GetOffset() - headerOffset ) - exportDataOffset;

			// Patch the export data in the file tables
			tables.PatchExport( index, exportDataOffset, exportDataSize, 0 );
		}	

		// unbind mapper
		file.m_mapper = nullptr;

		// exports saved
		return true;
	}

	namespace helper
	{
		static Bool IsFamilyLZ4( compression::ECompressionType type )
		{
			return type == compression::CT_LZ4 || type == compression::CT_LZ4HC;
		}

		static Bool IsFamilyKraken( compression::ECompressionType type )
		{
			return type == compression::CT_Kraken || type == compression::CT_KrakenHC;
		}

		// We assume that we're always saving in the HC version. HC only applies when compressing.
		// If we need to make sure then should force recompress everything.
		static Bool IsEquivalentToDefaultCompression( compression::ECompressionType type )
		{
			if ( type == compression::c_defaultCompressionType )
			{
				return true;
			}

			if ( IsFamilyLZ4( type ) && IsFamilyLZ4( compression::c_defaultCompressionType ) )
			{
				return true;
			}

			if ( IsFamilyKraken( type ) && IsFamilyKraken( compression::c_defaultCompressionType ) )
			{
				return true;
			}

			return false;
		}
	}

	Bool BinarySaver::WriteBuffers( IFile& file, const SavingContext& context, const StructureMapper& mapper, FileTablesBuilder& tables, const Uint64 headerOffset ) const
	{
		static_assert(compression::c_defaultCompressionType == compression::CT_KrakenHC, "");
		//static_assert(serialization::DeferredDataBuffer::c_defaultCompression == compression::CT_KrakenHC, "");
		RED_FATAL_ASSERT(serialization::DeferredDataBuffer::c_defaultCompression == compression::CT_KrakenHC, "");
		
		for ( Uint32 i = 0; i < mapper.m_buffers.Size(); ++i )
		{
			// Buffer is not extracted, save it at the end of the file
			const Uint32 bufferDataOffset = static_cast< Uint32 >( file.GetOffset() - headerOffset );

			const auto& mappedBuffer = mapper.m_buffers[i];
			ProcessedBufferInfo processedBuffer;
			if ( !ProcessBuffer_NoLock( file, context, mappedBuffer, processedBuffer ) )
			{
				return false;
			}

			file.Serialize(const_cast<void*>(processedBuffer.data), processedBuffer.diskSize);

			const Uint32 bufferSizeOnDisk = processedBuffer.diskSize;
			const Uint32 bufferCRC = processedBuffer.crc;

			// Make sure we advanced properly
			const Uint32 bufferDataEndOffset = static_cast< Uint32 >( file.GetOffset() - headerOffset );
			if ( bufferDataEndOffset != ( bufferDataOffset + bufferSizeOnDisk ) )
			{
				RED_LOG_ERROR( "Core: Failed to save buffer data of size %d to %hs - OUT OF DISK SPACE ?", bufferSizeOnDisk, file.GetFileNameForDebug() );
				return false;
			}
		
			// Patch up the buffer data in the file tables
			tables.PatchBuffer( i, bufferDataOffset, bufferSizeOnDisk, bufferCRC );
		}

		return true;
	}



	static bool ProcessBuffer_NoLock( const IFile& file, const SavingContext& context, const StructureMapper::BufferInfo& mappedBuffer, ProcessedBufferInfo& processedBuffer )
	{
		static const Bool allowQuickCompression = red::CommandLine::Get().HasOption("allowQuickCompression");


		// Sanity checks
		RED_FATAL_ASSERT( mappedBuffer.m_key != 0, "Mapped buffer with invalid ID" );
		RED_FATAL_ASSERT( mappedBuffer.m_inMemoryBuffer.Data() != nullptr, "Mapped buffers should never be empty" );
		RED_FATAL_ASSERT( mappedBuffer.m_inMemoryBuffer.GetSize() > 0, "Mapped buffers should never be empty" );
		RED_FATAL_ASSERT( mappedBuffer.m_sizeInMemoryDuringStructureMap == mappedBuffer.m_inMemoryBuffer.GetSize());
		RED_FATAL_ASSERT( mappedBuffer.m_sizePrecompressedForCookingDuringStructureMap == mappedBuffer.m_precompressedBufferForCooking.GetSize());

		// Check if need to recompress anyway
		Bool canUsePrecompressedBuffer = false;
		if ( mappedBuffer.m_precompressedBufferForCooking.Data() )
		{
			RED_FATAL_ASSERT( file.IsCooker(), "If not the cooker, then I don't know why you're using a precompressed buffer, so maybe a bug" );
			RED_FATAL_ASSERT( mappedBuffer.m_useCompression, "Tried to use pre-compressed onDiskBuffer with useCompression false" );
			RED_FATAL_ASSERT( mappedBuffer.m_precompressedBufferForCooking.GetSize() > 0 );
			RED_FATAL_ASSERT( mappedBuffer.m_precompressedBufferForCooking.GetSize() < mappedBuffer.m_inMemoryBuffer.GetSize(), "Bad compression. Precompressed buffer not smaller than in memory" );

			const auto compressionType = compression::GetCompressionTypeFromData( mappedBuffer.m_precompressedBufferForCooking.Data(), mappedBuffer.m_precompressedBufferForCooking.GetSize() );
			RED_FATAL_ASSERT( compressionType != compression::CT_Uncompressed && compressionType != compression::CT_MAX, "Unknown compression type %u. If removing a compression scheme, first resave the data", compressionType );

			// Piecemeal kraken migration: once kraken always kraken
			canUsePrecompressedBuffer = helper::IsFamilyKraken( compressionType ) || helper::IsEquivalentToDefaultCompression( compressionType );
			if (mappedBuffer.m_allowNonDefaultCompressionType)
			{
				// Override and allow it. Potential reasons such as faster cooking times until resaving data.
				canUsePrecompressedBuffer = true;
			}

			if (mappedBuffer.m_magicFlagForTerrain)
			{
				canUsePrecompressedBuffer = true;
			}

			if ( !canUsePrecompressedBuffer )
			{
				RED_LOG_SPAM( "Can't use precompressed buffer for cooking; cooking will be slower. Compression type changed from %u to %u.", compressionType, DeferredDataBuffer::c_defaultCompression );
			}

			// Override everything above. No more messing around. You're KrakenHC now.
			if (!allowQuickCompression)
			{
				if (compressionType != serialization::DeferredDataBuffer::c_defaultCompression)
				{
					canUsePrecompressedBuffer = false;
				}
			}
		}

		if ( canUsePrecompressedBuffer )
		{
			RED_FATAL_ASSERT( mappedBuffer.m_precompressedBufferForCooking.Data() );

			// Save data
			processedBuffer.data = mappedBuffer.m_precompressedBufferForCooking.Data();
			processedBuffer.diskSize = mappedBuffer.m_precompressedBufferForCooking.GetSize();
			processedBuffer.crc = red::CalculateCRC32(processedBuffer.data, processedBuffer.diskSize);
		}
		else if ( !mappedBuffer.m_useCompression )
		{
			// Save data
			processedBuffer.data = mappedBuffer.m_inMemoryBuffer.Data();
			processedBuffer.diskSize = mappedBuffer.m_inMemoryBuffer.GetSize();
		}
		else
		{
			RED_SET_SCOPED_CRASH_DATA(dd::compressingFile, file.GetFileNameForDebug());

			auto compressionType = DeferredDataBuffer::c_defaultCompression;
			if (allowQuickCompression)
			{
				if (mappedBuffer.m_magicFlagForTerrain)
				{
					compressionType = compression::CT_LZ4; // magic try Kraken non-HC later 
				}
				else if ( context.m_isCooker || context.m_isGenerator )
				{
					compressionType = compression::CT_Kraken; // cooked appearance meshes explode in size if we use LZ4
				}
			}

			auto compressedBuffer = compression::CompressData( compressionType, mappedBuffer.m_inMemoryBuffer.Data(), mappedBuffer.m_inMemoryBuffer.GetSize(), compression::GetDefaultCompressionAllocator() );
			if ( !compressedBuffer )
			{
				RED_LOG_ERROR( "Core: Failed to compress buffer data of size %d (%hs)", mappedBuffer.m_inMemoryBuffer.GetSize(), file.GetFileNameForDebug() );
				return false;
			}

			// Sanity check we can decompress the buffer later
			if ( context.m_debugVerifyBufferCompression )
			{
				auto sanityCheckDecompressedBuffer = compression::DecompressData(compressionType, compressedBuffer->GetData(), compressedBuffer->GetDataSize(), compression::GetDefaultCompressionAllocator());
				if (!sanityCheckDecompressedBuffer)
				{
					RED_LOG_ERROR("Core: Failed to decompress compressed buffer data of size %d (%hs)", mappedBuffer.m_inMemoryBuffer.GetSize(), file.GetFileNameForDebug());
					return false;
				}

				if (sanityCheckDecompressedBuffer->GetDataSize() != mappedBuffer.m_inMemoryBuffer.GetSize())
				{
					RED_LOG_ERROR("Core: Size mismatch. Failed to decompress compressed buffer data of size %d (%hs)", mappedBuffer.m_inMemoryBuffer.GetSize(), file.GetFileNameForDebug());
					return false;
				}

				if (red::Memcmp(mappedBuffer.m_inMemoryBuffer.Data(), sanityCheckDecompressedBuffer->GetData(), mappedBuffer.m_inMemoryBuffer.GetSize()) != 0)
				{
					RED_LOG_ERROR("Core: Decompressed buffer does not match original data of size %d (%hs)", mappedBuffer.m_inMemoryBuffer.GetSize(), file.GetFileNameForDebug());
					return false;
				}
			}

			if ( static_cast< Uint32 >( compressedBuffer->GetDataSize() ) < mappedBuffer.m_inMemoryBuffer.GetSize() )
			{
				processedBuffer.pinnedBuffer = compressedBuffer;
				processedBuffer.data = compressedBuffer->GetData();
				processedBuffer.diskSize = static_cast<Uint32>( compressedBuffer->GetDataSize() );
			}
			else
			{
				// No sense compressing the data if same size or bigger
				RED_LOG_TRACE( "Core: Skipped compressing buffer data of size %u (%hs). Is data already compressed?", mappedBuffer.m_inMemoryBuffer.GetSize(), file.GetFileNameForDebug() );
				processedBuffer.data = mappedBuffer.m_inMemoryBuffer.Data();
				processedBuffer.diskSize = mappedBuffer.m_inMemoryBuffer.GetSize();
			}
		}

		processedBuffer.crc = red::CalculateCRC32( processedBuffer.data, processedBuffer.diskSize );
		return true;
	}
	
	static bool WriteBuffers_NoLock( IFile& file, const SavingContext& context, const StructureMapper& mapper, const red::DynArray< ProcessedBufferInfo >& processedBuffers, FileTablesBuilder& tables, Uint64 headerOffset )
	{
		for ( Uint32 i = 0; i < mapper.m_buffers.Size(); ++i )
		{
			// Buffer is not extracted, save it at the end of the file
			const Uint32 bufferDataOffset = static_cast< Uint32 >( file.GetOffset() - headerOffset );

			auto& mappedBuffer = mapper.m_buffers[i];
			auto& processedBuffer = processedBuffers[i];

			file.Serialize(const_cast<void*>(processedBuffer.data), processedBuffer.diskSize);

			const Uint32 bufferSizeOnDisk = processedBuffer.diskSize;
			const Uint32 bufferCRC = processedBuffer.crc;

			// Make sure we advanced properly
			const Uint32 bufferDataEndOffset = static_cast< Uint32 >( file.GetOffset() - headerOffset );
			if ( bufferDataEndOffset != ( bufferDataOffset + bufferSizeOnDisk ) )
			{
				RED_LOG_ERROR( "Core: Failed to save buffer data of size %d to %hs - OUT OF DISK SPACE ?", bufferSizeOnDisk, file.GetFileNameForDebug() );
				return false;
			}

			// Patch up the buffer data in the file tables
			tables.PatchBuffer( i, bufferDataOffset, bufferSizeOnDisk, bufferCRC );
		}

		return true;
	}

#ifndef NO_EDITOR

	struct SaveObjectsContext
	{
		IFile& m_file;
		SavingContext m_savingContext;
		StructureMapper m_structureMapper;
		FileTables m_fileTables;
		red::UniquePtr< FileTablesBuilder > m_fileTablesBuilder;
		red::DynArray< ProcessedBufferInfo > m_bufferInfos{ red::PoolEngine() };

		SaveObjectsContext( IFile& file, const SavingContext& context )
			: m_file( file )
			, m_savingContext( context )
		{}
	};

	Bool BinarySaver::SaveObjects( IFile& file, const SavingContext& context, job::Builder& builder, bool* saveResult ) const
	{
		auto jobContext = red::CreateSharedPtr<SaveObjectsContext, red::PoolEngine>( file, context );

		if ( saveResult )
		{
			*saveResult = false;
		}

		// analyze structure of objects to save
		auto& structureMapper = jobContext->m_structureMapper;
		structureMapper.MapObjects( context );

		if ( structureMapper.m_names.Size() >= static_cast<Uint32>(std::numeric_limits<IMapper::NameIndex>::max() - 2) )
		{
			RED_LOG_ERROR( "BinarySaver: Too many names (%u) in resource '%hs'", structureMapper.m_names.Size(), file.GetFileNameForDebug() );
			return false;
		}
		if ( structureMapper.m_imports.Size() >= static_cast<Uint32>(std::numeric_limits<IMapper::PathIndex>::max() - 2) )
		{
			RED_LOG_ERROR( "BinarySaver: Too many imports (%u) in resource '%hs'", structureMapper.m_imports.Size(), file.GetFileNameForDebug() );
			return false;
		}

		// build serialization tables using the captured data
		auto& fileTables = jobContext->m_fileTables;
		jobContext->m_fileTablesBuilder = red::MakeUniquePtr( RED_NEW(FileTablesBuilder, red::PoolEngine)( fileTables, structureMapper ) ); // TODO(dg): move to member function of context?
		auto& tableBuilder = *jobContext->m_fileTablesBuilder;

		SetupNames( structureMapper, tableBuilder );
		SetupImports( structureMapper, tableBuilder );
		SetupExports( structureMapper, tableBuilder );
		SetupBuffers( structureMapper, tableBuilder );
		SetupInplaceResources( structureMapper, tableBuilder );

		// Store the GPU video memory cost
		fileTables.m_gpuSize = context.m_gpuSize;

		// Write the initial header, all offsets are RELATIVE to the header, not to the file start
		const Uint64 headerOffset = file.GetOffset();
		fileTables.Save( file, headerOffset );

		// Write exports
		if ( !WriteExports( file, context, structureMapper, tableBuilder, headerOffset ) )
		{
			return false;
		}

		// Write buffers
		jobContext->m_bufferInfos.Resize( structureMapper.m_buffers.Size() );
		for ( Uint32 i = 0; i < structureMapper.m_buffers.Size(); ++i )
		{
			auto& mappedBuffer = structureMapper.m_buffers[i];
			auto& processedBuffer = jobContext->m_bufferInfos[i];
			builder.DispatchJob<job::Fence::None>( "BinarySaver_SaveObjects_ProcessBuffer", [&mappedBuffer, &processedBuffer, jobContext, saveResult]( const job::RunContext& )
			{
				const auto& file = jobContext->m_file;
				const auto& context = jobContext->m_savingContext;
				if ( !ProcessBuffer_NoLock( file, context, mappedBuffer, processedBuffer ) )
				{
					// Set failed state here on something
					if ( saveResult )
					{
						*saveResult = false;
					}
				}
			} );
		}
		builder.DispatchFenceExplicitly();

		builder.DispatchJob( "BinarySaver_SaveObjects_WriteBuffers", [headerOffset, jobContext, saveResult]( const job::RunContext& )
		{
			auto& file = jobContext->m_file;
			const auto& context = jobContext->m_savingContext;
			auto& mapper = jobContext->m_structureMapper;
			auto& processedBuffers = jobContext->m_bufferInfos;
			auto& tableBuilder = *jobContext->m_fileTablesBuilder;

			if ( !WriteBuffers_NoLock( file, context, mapper, processedBuffers, tableBuilder, headerOffset ) )
			{
				if ( saveResult )
				{
					*saveResult = false;
				}
			}
		} );

		builder.DispatchJob( "BinarySaver_SaveObjects_WriteHeader", [headerOffset, jobContext, saveResult]( const job::RunContext& )
		{
			// TODO: Could merge this job with previous
			auto& file = jobContext->m_file;
			auto& fileTables = jobContext->m_fileTables;

			// Resave header, now with full set of data
			const Uint64 endOffset = file.GetOffset();
			file.Seek( headerOffset );
			fileTables.Save( file, headerOffset );
			file.Seek( endOffset );

			if ( saveResult )
			{
				*saveResult = true;
			}
		} );

		// objects saved
		if ( saveResult )
		{
			*saveResult = true;
		}
		return true;
	}

#endif // NO_EDITOR

} // serialization
