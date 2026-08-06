/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "serializationBinaryStructureMapper.h"
#include "serializationSaver.h"
#include "../../redFileSystem/include/nullFile.h"
#include "../../redSystem/include/crc.h"
#include "rttiProperty.h"
#include "pathResolver.h"

#define TO_EXPORT_INDEX( x ) ( ((Int32)x)+1 )
#define TO_IMPORT_INDEX( x ) ( -(((Int32)x) + 1 ) )
#define FROM_EXPORT_INDEX( x ) ( ((Int32)x) - 1 )
#define FROM_IMPORT_INDEX( x ) ( -((Int32)x) - 1 )

namespace serialization
{
	// ctremblay: Object can change state while being saved. This is the very bad. (PreSave, SetParent etc...)
	// For now, there is a global lock to make sure object state do not get corrupted.
	red::Mutex g_hackMapperLock; 

	StructureMapper::StructureMapper() = default;

	void StructureMapper::Reset()
	{
		m_exports.Clear();
		m_imports.Clear();
		m_names.Clear();
		m_properties.Clear();
		m_buffers.Clear();
		m_objectIndices.Clear();
		m_nameIndices.Clear();
		m_bufferIndices.Clear();
		m_importIndices.Clear();
		m_tempExports.Clear();
		m_tempObjectIndices.Clear();
	}

	void StructureMapper::MapObjects( const SavingContext& context )
	{
		// cleanup
		Reset();

		// Add empty entries (for cases when 0 means invalid entry)
		m_context = &context;
		m_names.PushBack( CName() );
		m_properties.PushBack( 0 );

		if( !context.m_isGenerator )
		{
			g_hackMapperLock.Acquire();	
		}

		// Map dependencies of initial objects as forced exports
		const Uint32 numInitialObjects = m_context->m_initialExports.Size();

		RED_FATAL_ASSERT( numInitialObjects > 0, "No object to maps. Invalid Behavior" );

		for( Uint32 i = 0; i < numInitialObjects; i++ )
		{
			auto& object = m_context->m_initialExports[ i ];
			if( object->IsA< CResource >() )
			{
				const rtti::ClassType* objectClassType = object->GetClass();
				objectClassType->RebuildParentHierarchy( object.Get(), nullptr );
			}
		}

		auto& object = m_context->m_initialExports.Front();
		ObjectIndex rootObjectIndex;
		MapPointer( object, rootObjectIndex );

		for( Uint32 inplaceResourceIndex = 1, end = numInitialObjects; inplaceResourceIndex != end; ++inplaceResourceIndex )
		{
			auto& inplaceObject = m_context->m_initialExports[ inplaceResourceIndex ];
			if( inplaceObject->IsA< CResource >() )
			{
				const THandle< CResource > resource = red::StaticCast< CResource >( inplaceObject );
				const res::ResourcePath& path = resource->GetPath();
				if( path.IsValid() )
				{
					PathIndex importIndex = -1;
					ObjectIndex index = -1;
					MapResourceReference( path, importIndex );
					MapPointer( inplaceObject, index );	
					m_inplaceResources.PushBack( { importIndex, FROM_EXPORT_INDEX( index ) } );
				}
			}
			else
			{
				ObjectIndex objectIndex;
				MapPointer( inplaceObject, objectIndex );
			}
		}
		
		// Sort mapping tables
		for ( Uint32 i=0; i<m_tempExports.Size(); i++ )
		{
			MapFinalObject( m_tempExports[i] );
		}

		if( !context.m_isGenerator )
		{
			g_hackMapperLock.Release();
		}

#ifndef RED_CONFIGURATION_FINAL
		if ( m_tempExports.Size() != m_exports.Size() )
		{
			const Uint32 exportsSize = m_exports.Size();
			const Uint32 tempExportSize = m_tempExports.Size();
			for ( Uint32 i = 0; i < tempExportSize; ++i )
			{
				if ( i >= exportsSize )
				{
					RED_LOG_ERROR( "StructureMapper: Temp exports out of range %s", m_tempExports[ i ] != nullptr ? m_tempExports[ i ]->GetFriendlyName().AsChar() : "nullptr" );
				}
				else if ( m_tempExports[ i ] != m_exports[ i ] )
				{
					RED_LOG_ERROR( "StructureMapper: Difference between temp exports and export in %s", m_tempExports[ i ] != nullptr ? m_tempExports[ i ]->GetFriendlyName().AsChar() : "nullptr" );
				}
			}
		}
#endif // RED_CONFIGURATION_FINAL

		// Sanity check
		RED_FATAL_ASSERT( m_tempExports.Size() == m_exports.Size(), "Mapping error" );
		RED_FATAL_ASSERT( m_objectIndices.Size() == m_tempObjectIndices.Size(), "Mapping error"  );
	}

	void StructureMapper::MapName( const CName& name, NameIndex& outIndex )
	{
		// Do not map already mapped objects
		Int32 nameIndex = 0;
		if ( name && !m_nameIndices.Find( name, nameIndex ) )
		{
			// Map
			nameIndex = m_names.Size();
			m_names.PushBack( name );

			// Map it to an index
			m_nameIndices.Insert( name, nameIndex );
		}

		outIndex = (NameIndex)nameIndex;
	}

	void StructureMapper::MapType( const rtti::IType* rttiType, TypeIndex& outIndex )
	{
		if ( rttiType )
		{
			MapName( rttiType->GetName(), (TypeIndex&) outIndex );
		}
		else
		{
			outIndex = 0;
		}
	}

	void StructureMapper::MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex )
	{
		Int32 objectIndex = 0;

		// Some objects should not be mapped
		if ( ShouldMapObject( objectRef ) )
		{
			// Do not map objects that are already mapped
			if ( !m_tempObjectIndices.Find( objectRef.Get(), objectIndex ) )
			{
				// Add to tables
				if ( ShouldExportObject( objectRef ) )
				{
					// Add object to export table
					objectIndex = TO_EXPORT_INDEX( m_tempExports.Size() );
					m_tempExports.PushBack( objectRef );
					m_tempObjectIndices.Insert( objectRef.Get(), objectIndex );

					// Map class name
					{
						NameIndex dummNameIndex;
						MapType( objectRef->GetClass(), dummNameIndex );
					}

					{
						// Map parent object, prevents missing spots and keeps the export list parent<child ordered
						ISerializable* serializableParent = objectRef->GetParent();
						if ( serializableParent != nullptr && !m_context->m_isCloner && !m_context->m_isCooker ) // ctremblay: If cloning, disregard parenting.
						{
							ObjectIndex parentObjectIndex;
							MapPointer( HandleFromPtr( serializableParent ), parentObjectIndex );
						}


						PreSaveContext context{ m_context->m_isCooker, m_context->m_isGenerator, m_context->m_isCloner, m_context->m_cookingPlatform };
						// Notify object that it's going to be saved
						objectRef->OnPreSave( context );

						// Serialize (recursive)
						CNullFileWriter nullFile;
						nullFile.m_mapper = this;
						nullFile.SetCooker( m_context->m_isCooker );
						nullFile.SetCloner( m_context->m_isCloner );
						if( m_context->m_magicFlagForAudioDefaultObject )
						{
							static_cast< ISerializable* >( objectRef->GetClass()->GetDefaultObject() )->OnSerialize( nullFile );
						}
						else
						{
							objectRef->OnSerialize( nullFile );
						}
					}
				}
				else
				{
					// Every import should be a resource
					auto resource = Cast< CResource >( objectRef );
					if ( resource && resource->GetPath().IsValid() )
					{
						RED_FATAL( "THandle of resource pointing to disk resource is not supported anymore. Use TResRef instead." );
					}
				}
			}
		}
		else
		{
			// do not map in the future
			m_objectIndices.Insert( objectRef.Get(), 0 );
			m_tempObjectIndices.Insert( objectRef.Get(), objectIndex );
		}

		// add to mapping
		outIndex = (ObjectIndex) objectIndex;
	}

	void StructureMapper::MapResourceReference( const res::ResourcePath& inPath, PathIndex& outIndex )
	{
		Int32 importIndex = 0;

		res::ResourcePath path = inPath;
#ifndef RED_CONFIGURATION_FINAL
		if ( s_pathResolver != nullptr )
		{
			path = s_pathResolver->ResolvePath( inPath );
		}
#endif

		if ( path.IsValid() && !path.IsEmpty() )
		{
			const auto pathHash = path.GetHash();

			const Bool found = m_importIndices.Find( pathHash, importIndex );
			if ( !found )
			{
				importIndex = TO_EXPORT_INDEX( m_imports.Size() );

				ImportInfo info{ path, false };
				m_imports.PushBack( info );
				m_importIndices[ pathHash ] = importIndex;
			}
			else
			{
				// we need this file preloaded
				m_imports[ FROM_EXPORT_INDEX( importIndex ) ].m_soft = false;
			}
		}

		outIndex = static_cast< PathIndex >( importIndex );
	}

	void StructureMapper::MapResourceDeferredReference( const res::ResourcePath& inPath, PathIndex& outIndex )
	{
		Int32 importIndex = 0;

		res::ResourcePath path = inPath;
#ifndef RED_CONFIGURATION_FINAL
		if ( s_pathResolver != nullptr )
		{
			path = s_pathResolver->ResolvePath( inPath );
		}
#endif

		if ( path.IsValid() && !path.IsEmpty() )
		{
			const auto pathHash = path.GetHash();

			if ( !m_importIndices.Find( pathHash, importIndex ) )
			{
				importIndex = TO_EXPORT_INDEX( m_imports.Size() );

				ImportInfo info{ path, true };
				m_imports.PushBack( info );
				m_importIndices[ pathHash ] = importIndex;
			}
		}

		outIndex = static_cast< PathIndex >( importIndex );
	}

	void StructureMapper::MapBuffer( const MapBufferParam& param, BufferIndex& outIndex )
	{
		Int32 bufferIndex = 0;

		// map only non-empty buffers
		if ( param.inMemoryData.Data() )
		{
			// compute the CRC of the data in the buffer, this will be part our KEY
			const Uint32 crc = red::CalculateCRC32( param.inMemoryData.Data(), param.inMemoryData.GetSize() );
			RED_FATAL_ASSERT( crc != 0, "Invalid CRC computed from non-zero buffer content" );

			// the content may be already mapped
			if ( !m_bufferIndices.Find( crc, bufferIndex ) )
			{
				// add to table of buffers
				BufferInfo info;
				info.m_inMemoryBuffer = param.inMemoryData;
				info.m_key = crc;

				info.m_useCompression = param.flags.useCompression;
				info.m_sizeInMemoryDuringStructureMap = param.inMemoryData.GetSize();
				info.m_allowNonDefaultCompressionType = param.flags.allowNonDefaultCompressionType;

				RED_FATAL_ASSERT(m_context);
				info.m_magicFlagForTerrain = m_context->m_magicFlagForTerrain;

				if ( param.flags.useFastCompression )
				{
					info.m_magicFlagForTerrain = true;
				}

				if ( param.flags.useCompression )
				{
					info.m_precompressedBufferForCooking = param.precompressedDataForCooking;
					info.m_sizePrecompressedForCookingDuringStructureMap = param.precompressedDataForCooking.GetSize();
				}

				info.m_hintGPUMemory = param.flags.hintGPUMemory;
				
				info.m_hintAutoLoadPC = param.flags.hintAutoLoadPC;
				info.m_hintAutoLoadXboxOne = param.flags.hintAutoLoadXboxOne;
				info.m_hintAutoLoadPS4 = param.flags.hintAutoLoadPS4;

				m_buffers.PushBack( info );
				bufferIndex = m_buffers.Size(); // +1 because index 0 means no data payload saved

				m_bufferIndices.Insert( crc, bufferIndex );
			}
			else
			{
				const Uint32 arrayIndex = bufferIndex - 1; // bufferIndex is +1, see above
				auto& buf = m_buffers[ arrayIndex ];
				RED_FATAL_ASSERT( buf.m_key == crc, "Expected CRC match at array index %u: %u vs %u", arrayIndex, buf.m_key, crc );

				if ( buf.m_useCompression != param.flags.useCompression )
				{
					RED_LOG_INFO( "Buffers at array index %u (crc=0x%08X) contain matching data, but with different useCompression options", arrayIndex, crc );
				}
				
				// DDBF_SaveNoCompression always wins
				if ( !param.flags.useCompression )
				{
					buf.m_useCompression = false;
					buf.m_precompressedBufferForCooking = nullptr;
					buf.m_sizePrecompressedForCookingDuringStructureMap = 0;
				}

				// Once terrain always terrain? Probably...
				RED_FATAL_ASSERT(m_context);
				if (m_context->m_magicFlagForTerrain)
				{
					buf.m_magicFlagForTerrain = true;
				}

				// #tbd: a hint is only valid if for all... for now assert since shouldn't be mapping same content differently
				//RED_FATAL_ASSERT(buf.m_hintGPUMemory == param.flags.hintGPUMemory, "GPU memory hint differs");
				//RED_FATAL_ASSERT(buf.m_hintStreamed == param.flags.hintStreaming, "Streaming hint differs");
				buf.m_hintGPUMemory &= param.flags.hintGPUMemory;
				buf.m_hintAutoLoadPC |= param.flags.hintAutoLoadPC;
				buf.m_hintAutoLoadXboxOne |= param.flags.hintAutoLoadXboxOne;
				buf.m_hintAutoLoadPS4 |= param.flags.hintAutoLoadPS4;
			}
		}

		outIndex = (BufferIndex) bufferIndex;
	}

	Bool StructureMapper::FindObjectIndex( const ISerializable* const object, Int32& outIndex ) const
	{
		return m_objectIndices.Find( object, outIndex );
	}

	Bool StructureMapper::FindImportIndex( const res::ResourcePath& inPath, Int32& outIndex ) const
	{
		res::ResourcePath path = inPath;
#ifndef RED_CONFIGURATION_FINAL
		if ( s_pathResolver != nullptr )
		{
			path = s_pathResolver->ResolvePath( inPath );
		}
#endif

		return m_importIndices.Find( path.GetHash(), outIndex );
	}

	Bool StructureMapper::FindBufferIndex( Uint32 crc, Int32& outIndex ) const
	{
		return m_bufferIndices.Find( crc, outIndex );
	}

	Bool StructureMapper::IsCloner() const
	{
		RED_FATAL_ASSERT( m_context != nullptr );
		return m_context->m_isCloner;
	}

	Bool StructureMapper::IsCooker() const
	{
		RED_FATAL_ASSERT( m_context != nullptr );
		return m_context->m_isCooker;
	}

	namespace Helper
	{
		const Bool IsContained( const ISerializable* object, const ISerializable* root )
		{
			while ( object != nullptr )
			{
				if ( object == root )
				{
					return true;
				}

				object = object->GetParent();
			}

			return false;
		}
	}

	Bool StructureMapper::ShouldMapObject( const THandle< ISerializable >& object ) const
	{
		// Don't map NULL objects
		if ( !object )
			return false;

		// Do not map objects of transient classes
		if ( object->GetClass()->IsAlwaysTransient() )
			return false;

		const ISerializable* const parent = object->GetParent();
		const auto& initialExports = m_context->m_initialExports;

		// Map other objects only if they are contained inside any object of initial export set (if ANY hierarchy is constructed)
		if ( parent != nullptr && !m_context->m_isCloner ) // ctremblay: If cloning, content is always contained.
		{
			auto isContained = [ &object ]( const THandle< ISerializable >& exportObject )
			{
				return Helper::IsContained( object.Get(), exportObject.Get() );
			};

			if ( !std::any_of( initialExports.Begin(), initialExports.End(), isContained ) )
			{
				// Do not map this object, it lies outside the scope of initial export objects
				return false;
			}
		}

		if ( object->IsA( ClassID<CResource>() ) )
		{
			// Do not include links to resources that parent us
			auto isParent = [ &object ]( const THandle< ISerializable >& exportObject )
			{
				return exportObject != object && Helper::IsContained( exportObject.Get(), object.Get() );
			};

			if ( std::any_of( initialExports.Begin(), initialExports.End(), isParent ) )
			{
				return false;
			}
		}

		// Include
		return true;
	}

	Bool StructureMapper::ShouldExportObject( const THandle< ISerializable >& object ) const
	{
		// Resources are usually not exported
		auto resource = Cast< CResource >( object );
		if ( resource )
		{
			const auto& initialExports = m_context->m_initialExports;

			// Special case for embedded resources
			auto isContained = [ &resource ]( const THandle< ISerializable >& realObject )
			{
				return realObject && Helper::IsContained( resource.Get(), realObject.Get() );
			};

			if ( std::any_of( initialExports.Begin(), initialExports.End(), isContained ) )
			{
				return true;
			}

			// Export all embedded resources as well, not just ones associated with a file
			if (!resource->GetPath().IsValid() )
				return true;

			// Import resources
			return false;
		}

		// Rest of the objects usually is exported
		// This includes ISerializable objects as well
		return true;
	}

	Int32 StructureMapper::MapFinalObject(const THandle< ISerializable >& object )
	{
		// NULL pointer
		if ( !object )
			return 0;

		// Do not map already mapped objects, this also handles imports
		Int32 objectIndex = 0;
		if ( m_objectIndices.Find( object.Get(), objectIndex ) )
			return objectIndex;

		// Make sure object was initially mapped
		Int32 unsortedObjectIndex = 0;
		if ( !m_tempObjectIndices.Find( object.Get(), unsortedObjectIndex ) )
		{
			RED_FATAL(  "Unmapped object reached when sorting dependencies" );
			return 0;
		}

		// ISerializable parent
		ISerializable* serializableParent = object->GetParent();
		if ( serializableParent != nullptr && !m_context->m_isCloner && !m_context->m_isCooker ) // ctremblay: If cloning, disregard parenting.
		{
			MapFinalObject( HandleFromPtr( serializableParent ) );
		}

		// Add to final table
		objectIndex = TO_EXPORT_INDEX( m_exports.Size() );
		m_exports.PushBack( object );

		// Map it to an index
		m_objectIndices.Insert( object.Get(), objectIndex );
		return objectIndex;
	}

#ifndef RED_CONFIGURATION_FINAL
	PathResolver* StructureMapper::SetPathResolver( PathResolver* filter )
	{
		auto* oldResolver = s_pathResolver;
		s_pathResolver = filter;
		return oldResolver;
	}

	PathResolver* StructureMapper::s_pathResolver = nullptr;
#endif

	bool StructureMapper::IsRelinking() const
	{
#ifndef RED_CONFIGURATION_FINAL
		if( s_pathResolver )
		{
			return true;
		}
#endif

		return false;
	}

	res::ResourcePath StructureMapper::RelinkPath( const res::ResourcePath& path ) const
	{
#ifndef RED_CONFIGURATION_FINAL
		if( s_pathResolver )
		{
			return s_pathResolver->ResolvePath( path );
		}
#endif

		return path;
	}

} // serialization