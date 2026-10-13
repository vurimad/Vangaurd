/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyScannerImpl.h"

#include "../../backendEngine/include/resourceUtils.h"

#include "../../../common/redFileSystem/include/fileVersionList.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redFileSystem/include/memoryFileReader.h"

#include "../../../common/redReflection/include/serializationFileTables.h"
#include "../../../common/redReflection/include/containersSerialization.h"
#include "../../../common/redReflection/include/packageIterator.h"

#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../../common/resource/include/jsonParser.h"

#include "../../../common/worldEntities/include/entityLayout.h"
#include "../../../common/resourceMesh/include/meshMaterialBuffer.h"
#include "../../../common/world/include/worldPrefabInstanceData.h"

// For Binary CSV Resource
#include "../../../common/resource/include/2darray.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redContainers/include/blob.h"
#include "../../../common/redReflection/include/serializationDecompressor.h"

namespace dependency
{
namespace prv
{

//------------------------------------------------------------------------------

ScanResult ScanBinaryResource( red::DynArray< ScanDependency >& result, IFile& file, const res::ResourcePath path, bool isInnerResource = false );

bool SeekToNameIndex( Uint16 index, IFile& file, serialization::FileTables& parentTables )
{
	{
		Uint8 flags = 0;
		file << flags;
	}

	// Read properties list
	for( ; ; )
	{
		Uint16 nameIndex = 0;
		file << nameIndex;

		if( nameIndex == 0 )
		{
			return false;
		}

		Uint16 typeIndex = 0;
		file << typeIndex;
		Uint32 skipDistance = 0;
		file << skipDistance;

		if( nameIndex == index )
		{
			break;
		}

		// Skip distance in file includes itself
		skipDistance -= sizeof( skipDistance );
		file.Seek( file.GetOffset() + skipDistance );
	}

	return true;
}

void ReadDependenciesFromPackage( const red::Package& package, red::DynArray< ScanDependency >& result )
{
	result.Reserve( result.Size() + package.resourceTable.Size() );

	// Resolve the imports
	for( red::PackageResourceIterator iter( package ); iter.IsValid(); iter.Next() )
	{
		const ScanDependency::Type importType = iter.GetImportType() == red::PackageResourceImportType::PackageResourceImportType_Async ? ScanDependency::Type::SOFT : ScanDependency::Type::HARD;
		result.PushBack( { iter.GetPath(), importType, ScanDependency::Location::INNER, false } );
	}
}

namespace helper
{
	static Bool CustomSerializedDataBuffer(IFile& file, serialization::FileTables& parentTables, red::UniqueBuffer& outDataBuffer)
	{
		// Custom this, but with or without a mapper:
		//DataBuffer buffer;
		//buffer.Serialize(file);

		red::UniqueBuffer dataBuffer;

		Uint32 dataSizeOrBufferIndex = 0;
		file << dataSizeOrBufferIndex;

		if ((dataSizeOrBufferIndex & DataBuffer::c_bufferIndexFlag) != 0)
		{
			const auto index = static_cast<serialization::IMapper::BufferIndex>(dataSizeOrBufferIndex & ~DataBuffer::c_bufferIndexFlag);
			dataSizeOrBufferIndex = ~0U; // make unusable
			if (index > 0) // empty buffers are mapped to zero and don't really exist in the m_buffers table
			{
				const auto& buffer = parentTables.m_buffers[index - 1];
				const Bool isCompressed = buffer.m_dataSizeInMemory != buffer.m_dataSizeOnDisk;
				const Uint32 dataOffset = buffer.m_dataOffset;
				file.Seek(dataOffset);

				auto diskBuffer = red::CreateUniqueBuffer<red::PoolBackend>(buffer.m_dataSizeOnDisk, DataBuffer::c_defaultAlignment);
				file.Serialize(diskBuffer.Get(), diskBuffer.Size());

				if (isCompressed)
				{
					dataBuffer = red::CreateUniqueBuffer<red::PoolBackend>(buffer.m_dataSizeInMemory, DataBuffer::c_defaultAlignment);

					if (!serialization::Decompressor::DecompressData("", red::MakeBlobView(diskBuffer), red::MakeBlobSpan(dataBuffer)))
					{
						return false;
					}
				}
				else
				{
					dataBuffer = std::move(diskBuffer);
				}
			}
		}
		else // it's data size
		{
			dataBuffer = red::CreateUniqueBuffer<red::PoolBackend>(dataSizeOrBufferIndex, DataBuffer::c_defaultAlignment);
			file.Serialize(dataBuffer.Get(), dataBuffer.Size());
		}

		outDataBuffer = std::move(dataBuffer);
		return true;
	}
}

ScanResult ScanEntityPackage( Uint16 index, red::DynArray< ScanDependency >& result, IFile& file, serialization::FileTables& parentTables )
{
	if( !index )
	{
		return ScanResult::OK;
	}

	// Parse the object and find the data member in the file, assuming it's a buffer inline
	const auto& mainExport = parentTables.m_exports[0];
	file.Seek( mainExport.m_dataOffset );

	if ( !SeekToNameIndex( index, file, parentTables ) )
	{
		return ScanResult::DATA_ERROR;
	}

	red::UniqueBuffer entityBuffer;
	if ( !helper::CustomSerializedDataBuffer( file, parentTables, entityBuffer ) )
	{
		return ScanResult::DATA_ERROR;
	}

	auto entityView = red::MakeBlobView( entityBuffer );

	ent::EntityLayoutLoader loader;
	loader.Initialize( entityView );
	ent::EntityLayout layout = loader.LoadEntityLayout();

	ReadDependenciesFromPackage( layout.package, result );

	return ScanResult::OK;
}

//------------------------------------------------------------------------------

bool LooksLikeResourcePath( const red::StringView& pathStrView )
{
	bool containsDot = pathStrView.Find( '.' ) != red::StringView::npos;
	bool containsSep = pathStrView.Find( '\\' ) != red::StringView::npos;
	if ( ! (containsDot && containsSep) )
	{
		return false;
	}

	red::DynArray< red::StringView > pathComponents = red::paths::SplitPath( pathStrView );
	if ( pathComponents.Size() < 2 )
	{
		return false;
	}

	const auto& firstPath = pathComponents.Front();
	if ( firstPath == "base" || firstPath == "engine" || firstPath == "test" || firstPath == "user" || firstPath == "temp" || firstPath == "gyms"
		|| firstPath == "multi" || firstPath == "dlc" || firstPath.StartsWith( "ep" ) )
	{
		return true;
	}

	return false;
}

//------------------------------------------------------------------------------

ScanResult ScanEntityResource( red::DynArray< ScanDependency >& result, IFile& file, serialization::FileTables& parentTables )
{
	// Entity files store another resource file inside of member buffers which are different in the cooked and uncooked versions
	// We need to scan through to find this member and then inspect the buffer inside as another resource to collect the file tables 
	// from that and add them to this resources dependency list

	// Find the name index/id of the "m_data" member, stored as "data" in files
	Uint16 dataNameIndex = 0; // 0 is always the "null" string / name
	Uint16 instanceDataNameIndex = 0;
	Uint16 compiledDataNameIndex = 0;
	for ( Uint32 index = 0; index != parentTables.m_names.Size(); ++index )
	{
		if ( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "localData" ) == 0 )
		{
			dataNameIndex = static_cast<Uint16>( index );
		}
		else if( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "includeInstanceBuffer" ) == 0 )
		{
			instanceDataNameIndex = static_cast<Uint16>( index );
		}
		else if( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "compiledData" ) == 0 )
		{
			compiledDataNameIndex = static_cast<Uint16>( index );
		}
	}

	if ( ScanEntityPackage( dataNameIndex, result, file, parentTables ) == ScanResult::OK 
		&& ScanEntityPackage( instanceDataNameIndex, result, file, parentTables ) == ScanResult::OK 
		&& ScanEntityPackage( compiledDataNameIndex, result, file, parentTables ) == ScanResult::OK )
	{
		return ScanResult::OK;
	}

	return ScanResult::DATA_ERROR;
}

ScanResult ScanAppearanceResource( red::DynArray< ScanDependency >& result, IFile& file, serialization::FileTables& parentTables )
{
	// Appearance files store another resource file inside of member buffers which are different in the cooked and uncooked versions
	// We need to scan through to find this member and then inspect the buffer inside as another resource to collect the file tables 
	// from that and add them to this resources dependency list

	Uint16 compiledDataNameIndex = 0; // 0 is always the "null" string / name
	for ( Uint32 index = 0; index != parentTables.m_names.Size(); ++index )
	{
		if( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "compiledData" ) == 0 )
		{
			compiledDataNameIndex = static_cast<Uint16>( index );
		}
	}

	if ( ScanEntityPackage( compiledDataNameIndex, result, file, parentTables ) == ScanResult::OK )
	{
		return ScanResult::OK;
	}

	return ScanResult::DATA_ERROR;
}

//------------------------------------------------------------------------------

bool ReadMeshLocalMaterialHeader( IFile& file, mesh::LocalMaterialHeader& header, Uint16 sizeIndex, Uint16 offsetIndex )
{
	header.m_offset = 0;
	header.m_size = 0;

	{
		Uint8 flags = 0;
		file << flags;
	}

	for ( ; ; )
	{
		Uint16 nameIndex = 0;
		file << nameIndex;

		if ( nameIndex == 0 )
		{
			break;
		}

		Uint16 typeIndex = 0;
		file << typeIndex;
		Uint32 skipDistance = 0;
		file << skipDistance;

		if ( nameIndex == sizeIndex )
		{
			file << header.m_size;
		}
		else if ( nameIndex == offsetIndex )
		{
			file << header.m_offset;
		}
		else
		{
			// Unknown element in structure
			skipDistance -= sizeof( skipDistance );
			file.Seek( file.GetOffset() + skipDistance );
		}
	}

	return true;
}

ScanResult ScanMeshResource( red::DynArray< ScanDependency >& result, IFile& file, serialization::FileTables& parentTables )
{
	Uint16 rawDataIndex = 0;
	Uint16 rawDataHeadersIndex = 0;
	Uint16 localMaterialIndex = 0;
	Uint16 sizeIndex = 0;
	Uint16 offsetIndex = 0;
	for ( Uint32 index = 0; index != parentTables.m_names.Size(); ++index )
	{
		if ( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "localMaterialBuffer" ) == 0 )
		{
			localMaterialIndex = static_cast<Uint16>( index );
		}
		else if ( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "rawData" ) == 0 )
		{
			rawDataIndex = static_cast<Uint16>( index );
		}
		else if ( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "rawDataHeaders" ) == 0 )
		{
			rawDataHeadersIndex = static_cast<Uint16>( index );
		}
		else if ( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "offset" ) == 0 )
		{
			offsetIndex = static_cast<Uint16>( index );
		}
		else if ( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "size" ) == 0 )
		{
			sizeIndex = static_cast<Uint16>( index );
		}
	}

	if ( localMaterialIndex != 0 && rawDataIndex != 0 && rawDataHeadersIndex != 0 )
	{
		// Parse the object and find the data member in the file, assuming it's a buffer inline
		const auto& mainExport = parentTables.m_exports[0];
		file.Seek( mainExport.m_dataOffset );

		if ( !SeekToNameIndex( localMaterialIndex, file, parentTables ) )
		{
			return ScanResult::DATA_ERROR;
		}

		const auto localMaterialOffset = file.GetOffset();

		if ( !SeekToNameIndex( rawDataHeadersIndex, file, parentTables ) )
		{
			return ScanResult::DATA_ERROR;
		}

		// Manual array serialisation here
		red::DynArray< mesh::LocalMaterialHeader > headers;
		{
			Uint32 count = 0;
			file << count;

			if ( count != 0 )
			{
				headers.Resize( count );

				for ( Uint32 i = 0; i < count; ++i )
				{
					if ( !ReadMeshLocalMaterialHeader( file, headers[ i ], sizeIndex, offsetIndex ) )
					{
						return ScanResult::DATA_ERROR;
					}
				}
			}
		}

		file.Seek( localMaterialOffset );

		if ( !SeekToNameIndex( rawDataIndex, file, parentTables ) )
		{
			return ScanResult::DATA_ERROR;
		}

		red::UniqueBuffer buffer;
		if (!helper::CustomSerializedDataBuffer(file, parentTables, buffer))
		{
			return ScanResult::DATA_ERROR;
		}

		red::BlobView bufferView( buffer.Data(), buffer.Size() );

		for ( const auto& header : headers )
		{
			red::BlobView instanceView = bufferView.Range( header.m_offset, header.m_size );
			CMemoryFileReaderExternalBuffer reader{ instanceView.Data(), instanceView.Size() };

			red::DynArray< ScanDependency > instanceResult;

			auto scanResult = ScanBinaryResource( instanceResult, reader, res::ResourcePath() );
			if ( scanResult != ScanResult::OK )
			{
				return scanResult;
			}

			for ( auto& import : instanceResult )
			{
				if ( import.inplace )
				{
					return ScanResult::DATA_ERROR;
				}

				import.type = ScanDependency::Type::SOFT;
				result.PushBack( import );
			}
		}

	}

	return ScanResult::OK;
}

//------------------------------------------------------------------------------

ScanResult ScanPrefabResource( red::DynArray< ScanDependency >& result, IFile& file, serialization::FileTables& parentTables )
{
	// There's instance data in the prefab, then there are also buffers within nodes which also need to be scanned

	Uint16 instanceDataClassNameIndex = 0; // 0 is always the "null" string / name
	Uint16 bufferNameIndex = 0;
	for ( Uint32 index = 0; index != parentTables.m_names.Size(); ++index )
	{
		if( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "worldPrefabInstanceData" ) == 0 )
		{
			instanceDataClassNameIndex = static_cast<Uint16>( index );
		}
		if( red::StrcmpNC( &parentTables.m_strings[ parentTables.m_names[ index ].m_string ], "buffer" ) == 0 )
		{
			bufferNameIndex = static_cast<Uint16>( index );
		}
	}

	if ( instanceDataClassNameIndex == 0 )
	{
		return ScanResult::OK;
	}

	// Prefab instance data is stored as a set of exports in the file
	for ( const auto& exp : parentTables.m_exports )
	{
		if ( exp.m_className != instanceDataClassNameIndex )
		{
			continue;
		}

		file.Seek( exp.m_dataOffset );
		if ( !SeekToNameIndex( bufferNameIndex, file, parentTables ) )
		{
			continue;
		}

		// We don't cook prefabs, so for now it's safe to still raw deserialize a DataBuffer here without getting the buffer mapping.
		DataBuffer buffer;
		buffer.Serialize( file );

		world::PrefabInstancePackageLoader loader;
		red::BlobView data( buffer.Data(), buffer.Size() );
		loader.Initialize( data );
		auto package = loader.Load();

		ReadDependenciesFromPackage( package, result );
	}

	return ScanResult::OK;
}

//------------------------------------------------------------------------------

ScanResult ScanBinaryCSVResource( red::DynArray< ScanDependency >& result, IFile& file, red::BlobView buffer )
{
	// Just load this normally, no fast way to do it
	serialization::LoadingContext loadingContext;
	serialization::LoadingResult loadingResult;

	if ( !serialization::LoadFromMemory( buffer.Data(), buffer.Size(), loadingContext, loadingResult, true ) )
	{
		return ScanResult::DATA_ERROR;
	}

	if ( loadingResult.m_loadedRootObjects.Empty() )
	{
		return ScanResult::DATA_ERROR;
	}

	THandle< C2dArray > arrayResource = Cast< C2dArray >( loadingResult.m_loadedRootObjects[0] );
	for ( Uint32 row = 0; row < arrayResource->GetNumberOfRows(); ++row )
	{
		for ( Uint32 col = 0; col < arrayResource->GetNumberOfColumns(); ++col )
		{
			auto value = arrayResource->GetValueRef( col, row );
			if ( !value.Empty() && LooksLikeResourcePath( value ) )
			{
				result.PushBack( { res::ResourcePath::Build( value ), ScanDependency::Type::SOFT, ScanDependency::Location::PRIMARY, false } );
			}
		}
	}

	return ScanResult::OK;
}

//------------------------------------------------------------------------------

ScanResult ScanBinaryResource( red::DynArray< ScanDependency >& result, IFile& file, const res::ResourcePath path, bool isInnerResource )
{
	// Load the file tables
	Uint32 version;
	serialization::FileTables tables;
	if ( !tables.Load( file, version ) )
	{
		return ScanResult::TABLE_FAILURE;
	}
	if ( version < VER_MINIMAL || version > VER_CURRENT )
	{
		return ScanResult::INVALID_VERSION;
	}

	result.Reserve( result.Size() + tables.m_imports.Size() );

	// Resolve the imports
	for ( const auto& import : tables.m_imports )
	{
		const char* str = &tables.m_strings[ import.m_path ];
		const ScanDependency::Type importType = ( import.m_flags & serialization::FileTables::eImportFlags_Soft ) != 0 ? ScanDependency::Type::SOFT : ScanDependency::Type::HARD;
		const ScanDependency::Location location = isInnerResource ? ScanDependency::Location::INNER : ScanDependency::Location::PRIMARY;
		const bool inplaceImport = ( import.m_flags & serialization::FileTables::eImportFlags_Inplace ) != 0;
		result.PushBack( { res::ResourcePath::Build( str ), importType, location, inplaceImport } );
	}

	const red::StringView ext = red::paths::GetExtension( path.ToStringView() );
	if ( ext == "ent" )
	{
		return ScanEntityResource( result, file, tables );
	}
	else if ( ext == "mesh" || ext == "w2mesh" || ext == "physicalscene" )
	{
		return ScanMeshResource( result, file, tables );
	}
	else if ( ext == "prefab" )
	{
		return ScanPrefabResource( result, file, tables );
	}
	else if ( ext == "app" )
	{
		return ScanAppearanceResource( result, file, tables );
	}


	return ScanResult::OK;
}

//------------------------------------------------------------------------------

ScanResult ScanJsonResource( red::DynArray< ScanDependency >& result, IFile& file )
{
	String textData;
	textData.Resize( static_cast<Uint32>( file.GetSize() ) );
	file.Serialize( textData.AsChar(), file.GetSize() );
	if ( file.HasErrors() )
	{
		return ScanResult::IO_ERROR;
	}

	res::json::JsonParser parser;
	parser.SetDebugName( file.GetFileNameForDebug() );
	if ( !parser.ParseJsonFile( std::move( textData ) ) )
	{
		return ScanResult::PARSE_FAILURE;
	}
	if ( !parser.CreateImportTable() )
	{
		return ScanResult::TABLE_FAILURE;
	}

	const auto& asyncRefDeps = parser.GetDependencies();
	result.Reserve( result.Size() + asyncRefDeps.Size() );
	for ( const auto& importRef : asyncRefDeps )
	{
		// TODO: This is actualy incorrect, JSON resources can have more dependencies than in the import table
		// as they can be serialised inline the resource reference fields.
		// TODO: Improve json parsing to better extract out the dependencies
		result.PushBack( { importRef.GetPath(), ScanDependency::Type::SOFT, ScanDependency::Location::PRIMARY, false } );
	}

	return ScanResult::OK;
}

ScanResult ScanCSVResource( red::DynArray< ScanDependency >& result, IFile& file )
{
	red::Blob buffer;
	if ( !red::LoadFileToBuffer( file, buffer ) || buffer.Size() < 4 )
	{
		return ScanResult::IO_ERROR;
	}

	if ( buffer[0] == 'C' && buffer[1] == 'R' && buffer[2] == '2' && buffer[3] == 'W' )
	{
		return ScanBinaryCSVResource( result, file, buffer );
	}

	red::StringView csvFileData( buffer.Pointer<const char>(), buffer.Size() );
	const char* iter = csvFileData.Data();
	const char* const end = iter + csvFileData.Length();
	const char* start = iter;

	while ( iter <= end )
	{
		if ( *iter == ';' || *iter == '\r' || *iter == '\n' || *iter == '\0' )
		{
			auto element = red::StringView( start, iter - start );
			if ( !element.Empty() && LooksLikeResourcePath( element ) )
			{
				result.PushBack( { res::ResourcePath::Build( element ), ScanDependency::Type::SOFT, ScanDependency::Location::PRIMARY, false } );
			}
			start = iter + 1;
		}
		++iter;
	}

	return ScanResult::OK;
}

ScanResult ScanTweakResource( red::DynArray< ScanDependency >& result, IFile& file )
{
	String textData;
	if ( !red::LoadFileToString( file, textData ) )
	{
		return ScanResult::IO_ERROR;
	}

	const char* iter = textData.AsChar();
	const char* const end = iter + textData.Length();
	const char* start = nullptr;

	while ( iter <= end )
	{
		if ( *iter == '"' )
		{
			if ( start == nullptr )
			{
				start = iter + 1;
			}
			else
			{
				auto end = iter;
				if ( start < end )
				{
					auto str = red::StringView( start, end - start );
					if ( !str.Empty() && LooksLikeResourcePath( str ) )
					{
						result.PushBack( { res::ResourcePath::Build( str ), ScanDependency::Type::SOFT, ScanDependency::Location::PRIMARY, false } );
					}
				}
				start = nullptr;
			}
		}
		++iter;
	}

	return ScanResult::OK;
}

ScanResult ScanLinkResource( red::DynArray< ScanDependency >& result, IFile& file )
{
	String linkStr;
	if ( !red::LoadFileToString( file, linkStr ) )
	{
		return ScanResult::IO_ERROR;
	}

	if ( !LooksLikeResourcePath( linkStr ) )
	{
		return ScanResult::LINK_ERROR;
	}

	if ( linkStr.Length() > res::ResourcePath::MAX_LENGTH )
	{
		return ScanResult::LINK_ERROR;
	}

	result.PushBack( { res::ResourcePath::Build( linkStr ), ScanDependency::Type::HARD, ScanDependency::Location::PRIMARY, false } );

	return ScanResult::OK;
}

//------------------------------------------------------------------------------

ScanResult ScanResource( red::SharedPtr< IFile > file, const res::ResourcePath path, red::DynArray< ScanDependency >& result )
{
	if ( file->GetSize() == 0 )
	{
		return ScanResult::EMPTY;
	}

	if ( IsLinkResource( path ) )
	{
		return ScanLinkResource( result, *file );
	}
	else if ( IsJsonResource( path ) )
	{
		if ( ScanJsonResource( result, *file ) != ScanResult::OK )
		{
			file->Seek( 0ull );
			// Could be a cooked JSON resource so try to load it as binary
			return ScanBinaryResource( result, *file, path );
		}
		return ScanResult::OK;
	}
	else if ( IsTextResource( path ) )
	{
		if ( red::paths::GetExtension( path.ToStringView() ) == "csv" )
		{
			return ScanCSVResource( result, *file );
		}
		else if ( red::paths::GetExtension( path.ToStringView() ) == "tweak" )
		{
			return ScanTweakResource( result, *file );
		}
		// This is ok, we are a simple text resource with no dependencies
		return ScanResult::OK;
	}
	else if ( IsBinaryResource( path ) )
	{
		return ScanBinaryResource( result, *file, path );
	}
	else
	{
		return ScanResult::NOT_A_RESOURCE;
	}

	return ScanResult::OK;
}

//------------------------------------------------------------------------------

ScanResult ScanResource( res::IBackendResourceDepot* depot, const res::ResourcePath path, red::DynArray< ScanDependency >& result )
{
	auto file = depot->CreateResourceReader( path, false );
	if ( !file )
	{
		return ScanResult::FILE_NOT_FOUND;
	}

	return ScanResource( file, path, result );
}

ScanResult ScanResource( res::IBackendResourceDepot* depot, const res::ResourcePath path, red::DynArray< res::ResourcePath >& result )
{
	red::DynArray< prv::ScanDependency > scanned;

	ScanResult status = ScanResource( depot, path, scanned );

	result.Reserve( result.Size() + scanned.Size() );
	for ( const auto& dep : scanned )
	{
		result.PushBackUnchecked( dep.path );
	}

	return status;
}

} // prv
} // dependency
