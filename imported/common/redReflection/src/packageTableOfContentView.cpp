/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageTableOfContentView.h"
#include "package.h"
#include "packageIterator.h"

namespace red
{
	PackageTableOfContentView::PackageTableOfContentView( const Package & package )
		: m_package( &package )
	{}

	PackageTableOfContentView::~PackageTableOfContentView()
	{}

	bool PackageTableOfContentView::IsObjectTypeValid( ObjectIndex index ) const
	{
		PackageObjectIterator validator( *m_package );
		validator.SetCurrentIndex( index );
		return validator.IsTypeValid();
	}

	PackageTableOfContent::NameIndex PackageTableOfContentView::OnMapName( CName name )
	{
		RED_FATAL( "PackageTableOfContentView cannot map CName." );
		return ~0;
	}
	
	PackageTableOfContent::ObjectIndex PackageTableOfContentView::OnMapObject( const ISerializable * object, const void * referenceObject )
	{
		RED_FATAL( "PackageTableOfContentView cannot map Object" );
		return ~0;
	}

	PackageTableOfContent::ResourceIndex PackageTableOfContentView::OnMapResource( const res::ResourcePath & path, PackageResourceImportType importType )
	{
		RED_FATAL( "PackageTableOfContentView cannot map Resource" );
		return ~0;
	}

	CName PackageTableOfContentView::OnUnmapName( Uint16 index ) const
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

	void PackageTableOfContentView::OnUnmapObject( ObjectIndex index, SerializableHandle& handle ) const
	{
	}

	res::ResourcePath PackageTableOfContentView::OnUnmapResource( ResourceIndex index, res::ResourceTokenHandle & token ) const
	{
		PackageResourceIterator iter( *m_package );
		iter.SetIndex( index ); 
		return iter.GetPath();
	}

	PackageTableOfContent::ObjectIndex PackageTableOfContentView::OnRemapObject( ObjectIndex index )
	{
		return ~0;
	}

	PackageTableOfContent::NameIndex PackageTableOfContentView::OnRemapName( NameIndex )
	{
		return ~0;
	}

	PackageTableOfContent::ResourceIndex PackageTableOfContentView::OnRemapResource( ResourceIndex, PackageResourceImportType  )
	{
		return ~0;
	}

}
