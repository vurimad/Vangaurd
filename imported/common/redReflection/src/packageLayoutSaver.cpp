/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageLayoutSaver.h"
#include "packageStream.h"
#include "packageLayout.h"
#include "packageTableOfContent.h"
#include "packageVersion.h"
#include "packageTable.h"

namespace red
{
	PackageLayoutSaver::PackageLayoutSaver()
	{}

	PackageLayoutSaver::~PackageLayoutSaver()
	{}

	Uint32 PackageLayoutSaver::GetLayoutSize( const PackageTable & table ) const
	{
		const Uint32 tableCount = 
			!table.rootObjectTable.Empty()
			+ !table.resourceTable.Empty()
			+ !table.stringTable.Empty();

		const Uint32 tableSize = tableCount * sizeof( PackageLayoutEntry );

		return sizeof( PackageLayoutHeader ) + tableSize + OnGetUserLayoutSize();
	}

	struct WriteTableLayoutContext
	{
		const Uint64 relativeOffset;
		PackageStream & stream;
		Uint16 mask;
	};

	template< typename T >
	void WriteTableLayout( red::ArraySpan< T > table, WriteTableLayoutContext & context )
	{
		if( !table.Empty() )
		{	
			const Uint32 offset = static_cast< Uint32 >( red::memory::AddressOf( table.Data() ) - context.relativeOffset );
			PackageLayoutEntry entry = 
			{
				offset, 
				offset + table.SizeInBytes()
			};

			context.stream.Write( &entry, sizeof( entry ) );
			context.mask |= RED_FLAG( T::Type );
		}
	}

	void PackageLayoutSaver::WriteLayout( const Package& package, PackageStream & stream ) const
	{
		const Uint64 packageStartAddress = red::memory::AddressOf( package.buffer.Data() );

		stream.Skip( sizeof( PackageLayoutHeader ) );

		WriteTableLayoutContext context = 
		{
			packageStartAddress,
			stream,
			0
		};

		// ctremblay: THIS NEED TO BE WRITTEN IN SAME ORDER THAN DEFINED IN PackageTableType ENUM.
		WriteTableLayout( package.resourceTable, context );
		WriteTableLayout( package.stringTable, context );
		WriteTableLayout( package.objectTable, context );

		OnWriteUserLayout( stream );
	
		stream.Seek( 0 );

		PackageLayoutHeader header = 
		{
			c_packageCurrentVersion,
			context.mask,
			package.rootObjectTable.Size()
		};

		stream.Write( &header, sizeof( header ) );
	}
}
