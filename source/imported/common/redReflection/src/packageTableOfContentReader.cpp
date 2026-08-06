/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageTableOfContentReader.h"
#include "package.h"
#include "packageUtils.h"
#include "resourceToken.h"

namespace red
{
	PackageTableOfContentReader::PackageTableOfContentReader()
		: m_package( nullptr )
		, m_table( nullptr )
	{}

	PackageTableOfContentReader::~PackageTableOfContentReader()
	{}

	void PackageTableOfContentReader::Initialize( const Package & package, PackageTableReader & table )
	{
		m_package = &package;
		m_table = &table;
		m_table->objectTable.Resize( m_package->objectTable.Size() );
		m_table->resourceTable.Resize( m_package->resourceTable.Size() );
		m_resourceIterator.SetPackage( package );
	}

	PackageTableOfContent::NameIndex PackageTableOfContentReader::OnMapName( CName name )
	{
		RED_FATAL( "PackageTableOfContentReader cannot map CName." );
		return ~0;
	}
	
	PackageTableOfContent::ObjectIndex PackageTableOfContentReader::OnMapObject( const ISerializable * object, const void * referenceObject )
	{
		RED_FATAL( "PackageTableOfContentReader cannot map Object" );
		return ~0;
	}

	PackageTableOfContent::ResourceIndex PackageTableOfContentReader::OnMapResource( const res::ResourcePath & path, PackageResourceImportType importType )
	{
		RED_FATAL( "PackageTableOfContentReader cannot map Resource" );
		return ~0;
	}

	CName PackageTableOfContentReader::OnUnmapName( Uint16 index ) const
	{
		if( index < m_package->stringTable.Size() )
		{
			const StringDescriptor & descriptor = m_package->stringTable[ index ];

			// FIXME cannot use descriptor.size as strings which exceed maximum length are being stored and overflowing size
			//const Uint8 size = static_cast< Uint8 >( descriptor.size ) - 1; // Cast to ensure -1 rolls over to 255 instead of 0xffffffff, as 0 size means string length was 255 originally and +1 was added for null terminator
			//const red::StringView name{ m_package->buffer.Pointer< char >( descriptor.offset ), size };
			const red::StringView name{ m_package->buffer.Pointer< char >( descriptor.offset ) };

			return RED_NAME( name );
		}

		return CName();
	}

	void PackageTableOfContentReader::OnUnmapObject( ObjectIndex index, SerializableHandle & handle ) const
	{
		SerializableHandle & object = m_table->objectTable[ index ];
		if( !object )
		{
			if( !handle )
			{
				const ObjectDescriptor & descriptor = m_package->objectTable[ index ];
				const CName objectTypeName = UnmapName( descriptor.typeNameIndex );
				handle = CreateObject( objectTypeName );
			}
			else
			{ /* ctremblay: Object already exist. Should we override or apply ?! */ }

			object = handle;

			auto iter = std::find( m_table->pendingObjectContainer.Begin(), m_table->pendingObjectContainer.End(), index );
			if( iter == m_table->pendingObjectContainer.End() )
			{
				m_table->pendingObjectContainer.PushBack( index );
			}
		}
		else
		{
			handle = object;
		}
	}

	res::ResourcePath PackageTableOfContentReader::OnUnmapResource( ResourceIndex index, res::ResourceTokenHandle & token ) const
	{
		token = m_table->resourceTable[ index ];
		m_resourceIterator.SetIndex( index );
		return m_resourceIterator.GetPath();
	}

	PackageTableOfContent::ObjectIndex PackageTableOfContentReader::OnRemapObject( ObjectIndex index )
	{
		return ~0;
	}

	PackageTableOfContent::NameIndex PackageTableOfContentReader::OnRemapName( NameIndex )
	{
		return ~0;
	}

	PackageTableOfContent::ResourceIndex PackageTableOfContentReader::OnRemapResource( ResourceIndex, PackageResourceImportType )
	{
		return ~0;
	}

	red::UniquePtr< PackageTableOfContentReader > CreatePackageTableOfContentReader( const Package & package, PackageTableReader & table  )
	{
		red::UniquePtr< PackageTableOfContentReader > tableOfContentReader = red::CreateUniquePtr< PackageTableOfContentReader >();
		tableOfContentReader->Initialize( package, table );
		return tableOfContentReader;
	}
}
