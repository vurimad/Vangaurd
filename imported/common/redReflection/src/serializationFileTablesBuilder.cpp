/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "rttiClass.h"
#include "rttiProperty.h"
#include "serializationFileTables.h"
#include "serializationFileTablesBuilder.h"
#include "serializationBinaryStructureMapper.h"

namespace serialization
{
	FileTablesBuilder::FileTablesBuilder( FileTables& outData, const StructureMapper& structureMapper )
		: m_data( &outData )
	{
		// cleanup
		m_data->m_buffers.Clear();
		m_data->m_buffers.Reserve( structureMapper.m_buffers.Size() );
		m_data->m_exports.Clear();
		m_data->m_exports.Reserve( structureMapper.m_exports.Size() );
		m_data->m_imports.Clear();
		m_data->m_imports.Reserve( structureMapper.m_imports.Size() );
		m_data->m_names.Clear();
		m_data->m_names.Reserve( structureMapper.m_names.Size() );

		// Although the properties chunk is deprecated and no longer in use--it cannot be removed right now
		// as this would alter all of the resources and cause the patch size to equal the base size. A single
		// entry is expected so at least the reserved memory can be limited.
		m_data->m_properties.Clear();
		m_data->m_properties.Reserve( 1 );

		Uint32 stringReserve = 1; // First entry always the null terminator
		for( auto& nth : structureMapper.m_names )
		{
			stringReserve += nth.AsStringView().Length() + 1;
		}

		m_data->m_strings.Clear();
		m_data->m_strings.Reserve( stringReserve );
		m_data->m_inplace.Clear();
		m_data->m_inplace.Reserve( structureMapper.m_inplaceResources.Size() );

		m_nameMap.Reserve( structureMapper.m_names.Size() );

		// allocate empty string entry
		m_data->m_strings.PushBack( 0 );

		// allocate empty name entry
		FileTables::Name emptyName;
		emptyName.m_hash = 0;
		emptyName.m_string = 0;
		m_data->m_names.PushBack( emptyName );

		// See note above regarding the deprecated properties chunk. Allocate empty property entry.
		FileTables::Property emptyProp{};
		m_data->m_properties.PushBack( emptyProp );
	}

	FileTablesBuilder::~FileTablesBuilder() = default;

	Uint32 FileTablesBuilder::MapString( const String& string )
	{
		// empty string always maps to zero
		if ( string.Empty() )
		{
			return 0;
		}

		// find in map
		Uint32 index = 0;
		if ( m_stringMap.Find( string, index ) )
		{
			return index;
		}

		// append
		const Uint32 size = string.Length() + 1;
		const Uint32 offset = m_data->m_strings.Size();
		m_data->m_strings.Grow( size );
		red::Memcpy( &m_data->m_strings[ offset ], string.AsChar(), sizeof(AnsiChar) * size );

		// add to map
		m_stringMap.Insert( string, offset );
		return offset;
	}

	Uint16 FileTablesBuilder::AddName( const CName name )
	{
		RED_FATAL_ASSERT( name, "Adding empty name" );

		const auto index = (Uint16) m_data->m_names.Size();

		// add new name
		FileTables::Name nameData;
		nameData.m_hash = red::GetHash( name );
		nameData.m_string = MapString( name.AsStringView().ToString() );
		m_data->m_names.PushBack( nameData );

		// add to map
		m_nameMap.Insert( name, index );
		return index;
	}

	Uint16 FileTablesBuilder::MapName( const CName name ) const
	{
		// null name is always mapped to 0
		if ( !name )
		{
			return 0;
		}

		// find in map
		Uint16 index = 0;
		const Bool found = m_nameMap.Find( name, index );
		RED_FATAL_ASSERT( found, "Trying to save unmapped name: '%hs'", name.AsChar() );

		return index;
	}

	Uint32 FileTablesBuilder::AddImport( const ImportInfo& importInfo )
	{
		// invalid resource path
		if ( !importInfo.m_path.IsValid() )
		{
			return static_cast< Uint32 >( -1 );
		}

		// create import info
		FileTables::Import importData;
		importData.m_flags = 0;
		importData.m_deprecated0 = 0;
		importData.m_path = MapString( importInfo.m_path.ToString() );

		// setup flags
		if ( importInfo.m_isObligatory )
			importData.m_flags |= FileTables::eImportFlags_Obligatory;
		if ( importInfo.m_isSoft )
			importData.m_flags |= FileTables::eImportFlags_Soft;

		// add to list
		const Uint32 index = (Uint32) m_data->m_imports.Size();
		m_data->m_imports.PushBack( importData );
		return index;
	}

	Uint32 FileTablesBuilder::AddInplaceResource( const InplaceResourceInfo& inplaceResourceInfo )
	{
		FileTables::InplaceResource inplaceResource = {};
		inplaceResource.m_importIndex = inplaceResourceInfo.m_importIndex;
		inplaceResource.m_exportIndex = inplaceResourceInfo.m_exportIndex;
		m_data->m_inplace.PushBack( inplaceResource );
		m_data->m_imports[ inplaceResource.m_importIndex - 1 ].m_flags |= FileTables::eImportFlags_Inplace;

		return m_data->m_inplace.Size() - 1;
	}

	Uint32 FileTablesBuilder::AddExport( const ExportInfo& exportInfo )
	{
		// invalid class name
		if ( exportInfo.m_className.Empty() )
		{
			return static_cast< Uint32 >( -1 );
		}

		// create export info
		FileTables::Export exportData;
		red::Memzero( &exportData, sizeof(exportData) );
		exportData.m_className = MapName( exportInfo.m_className );
		exportData.m_parent = exportInfo.m_parent;

		// add to list
		const Uint32 index = (Uint32) m_data->m_exports.Size();
		m_data->m_exports.PushBack( exportData );
		return index;
	}

	Uint32 FileTablesBuilder::AddBuffer( const BufferInfo& bufferInfo )
	{
		// Allocate index
		const Uint32 bufferIndex = (Uint32) m_data->m_buffers.Size() + 1;

		// create export info
		FileTables::Buffer bufferData;
		red::Memzero( &bufferData, sizeof(bufferData) );
		bufferData.m_dataSizeInMemory = bufferInfo.m_dataSizeInMemory; 
		bufferData.m_dataSizeOnDisk = bufferInfo.m_dataSizeOnDisk; // may not be known yet: zero
		if (bufferInfo.m_hintGPUMemory)
		{
			bufferData.m_flags |= FileTables::eBufferFlags_HintGPUMemory;
		}

		if (bufferInfo.m_hintAutoLoadPC)
		{
			bufferData.m_flags |= FileTables::eBufferFlags_HintAutoLoadPC;
		}

		if (bufferInfo.m_hintAutoLoadXboxOne)
		{
			bufferData.m_flags |= FileTables::eBufferFlags_HintAutoLoadXboxOne;
		}

		if (bufferInfo.m_hintAutoLoadPS4)
		{
			bufferData.m_flags |= FileTables::eBufferFlags_HintAutoLoadPS4;
		}

		// add to list
		m_data->m_buffers.PushBack( bufferData );
		return bufferIndex;
	}

	void FileTablesBuilder::PatchExport( const Uint32 exportIndex, const Uint32 dataOffset, const Uint32 dataSize, const Uint32 dataCRC )
	{
		RED_FATAL_ASSERT( exportIndex < m_data->m_exports.Size(), "Export index out of range" );
		m_data->m_exports[ exportIndex ].m_dataOffset = dataOffset;
		m_data->m_exports[ exportIndex ].m_dataSize = dataSize;
		m_data->m_exports[ exportIndex ].m_crc = dataCRC;
	}

	void FileTablesBuilder::PatchBuffer( const Uint32 bufferIndex, const Uint32 dataOffset, const Uint32 dataSizeOnDisk, const Uint32 bufferCRC )
	{
		RED_FATAL_ASSERT( bufferIndex >= 0 && bufferIndex < m_data->m_buffers.Size(), "Buffer index out of range" );

		RED_FATAL_ASSERT( dataSizeOnDisk <= m_data->m_buffers[ bufferIndex ].m_dataSizeInMemory, "Buffer is smaller in memory than on disk - compression fuckup" );
		m_data->m_buffers[ bufferIndex ].m_dataOffset = dataOffset;
		m_data->m_buffers[ bufferIndex ].m_dataSizeOnDisk = dataSizeOnDisk;
		m_data->m_buffers[ bufferIndex ].m_crc = bufferCRC;
	}
	} // serialization