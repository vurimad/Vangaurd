/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageLayoutLoader.h"
#include "packageReadStream.h"
#include "packageLayout.h"
#include "packageVersion.h"
#include "../../redContainers/include/fixedArray.h"

namespace red
{
	PackageLayoutLoader::PackageLayoutLoader()
	{
	}

	PackageLayoutLoader::~PackageLayoutLoader()
	{}

	void PackageLayoutLoader::Initialize( red::BlobView data )
	{
		m_data = data;
	}

	template< typename T >
	red::ArraySpan< const T > ComputeTable( const Uint8 * startAddress, const PackageLayoutEntry & entry )
	{
		const T * begin = reinterpret_cast< const T* >( startAddress + entry.tableOffset );
		const T * end = reinterpret_cast< const T* >( startAddress + entry.dataOffset );

 		return red::MakeArraySpan( begin, end );
	}

	Package PackageLayoutLoader::Load()
	{
		if( m_data.Empty() )
		{
			return Package{ c_packageCurrentVersion };
		}

		PackageReadStream stream;
		stream.SetBuffer( m_data );

		PackageLayoutHeader header;
		stream.Read( &header, sizeof( PackageLayoutHeader ) );

		if( header.version == c_packageVersionSimpleLayout )
		{
			struct OldPackageLayout
			{
				Uint32 stringTableOffset;
				Uint32 stringDataOffset;
				Uint32 objectTableOffset;
				Uint32 objectDataOffset;
			};

			OldPackageLayout layout;
			stream.Read( &layout, sizeof( layout ) );
			OnReadUserLayout( header.version, stream );

			const Uint32 packageOffset = static_cast< Uint32 >( stream.GetPosition() );
			const Uint8 * startAddress = m_data.Pointer< Uint8 >( packageOffset );
 			const Uint8 * stringTableAddress = startAddress + layout.stringTableOffset;
 			const Uint8 * stringDataAddress = startAddress + layout.stringDataOffset;
 			const Uint8 * objectTableAddress = startAddress + layout.objectTableOffset;
 			const Uint8 * objectDataAddress = startAddress + layout.objectDataOffset;

			Package result =
			{
				header.version,
				Package::ObjectTable(reinterpret_cast< const ObjectDescriptor* >(objectTableAddress), reinterpret_cast< const ObjectDescriptor* >(objectDataAddress)),
				Package::ObjectTable(reinterpret_cast< const ObjectDescriptor* >(objectTableAddress), reinterpret_cast< const ObjectDescriptor* >(objectDataAddress)),
				Package::StringTable( reinterpret_cast< const StringDescriptor* >( stringTableAddress ), reinterpret_cast< const StringDescriptor* >( stringDataAddress ) ),
				Package::ResourceTable(),
				m_data.Range( packageOffset )
			};

			return result;
		}
		else
		{
			red::FixedArray< PackageLayoutEntry, PackageTableType_Count > tables = {};
			for( Uint32 index = 0; index != PackageTableType_Count; ++index )
			{
				if( header.tableMask & RED_FLAG( index ) )
				{
					stream.Read( &tables[ index ], sizeof( PackageLayoutEntry ) );
				}
			}

			OnReadUserLayout( header.version, stream );

			const Uint32 packageOffset = static_cast< Uint32 >( stream.GetPosition() );
			const Uint8 * startAddress = m_data.Pointer< Uint8 >( packageOffset );

			Package::ObjectTable allObjectTable = ComputeTable< ObjectDescriptor >( startAddress, tables[PackageTableType_Object] );
			Package::ObjectTable rootObjectTable = 
				header.version < c_packageVersionRootObjectLayout ? 
				allObjectTable :
				allObjectTable.Left( header.rootObjectCount );

			Package result = 
			{ 
				header.version,
				rootObjectTable,
				allObjectTable,
				ComputeTable< StringDescriptor >( startAddress, tables[ PackageTableType_String ] ),
				ComputeTable< ResourceDescriptor >( startAddress, tables[ PackageTableType_Resource ] ), 
				m_data.Range( packageOffset )
			};
			
			return result;
		}
	}
}

