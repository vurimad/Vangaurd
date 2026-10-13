/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageIterator.h"
#include "package.h"
#include "rttiSystem.h"

namespace red
{
	PackagePropertyIterator::PackagePropertyIterator( red::BlobView propertyData, PackageTableOfContent* table, PackagePropertyTable propertyTable, Uint32 version )
		: m_propertyData( propertyData )
		, m_table( table )
		, m_propertyTable( propertyTable )
		, m_index( 0 )
		, m_type( nullptr )
		, m_data( nullptr )
		, m_version( version )
	{
		ResolveContent();
	}

	PackagePropertyIterator::~PackagePropertyIterator()
	{}

	CName PackagePropertyIterator::GetName()
	{
		return m_name;
	}

	const rtti::IType* PackagePropertyIterator::GetType() const
	{
		return m_type;
	}

	const void* PackagePropertyIterator::GetData() const
	{
		return m_data;
	}

	void PackagePropertyIterator::Next()
	{
		++m_index;
		ResolveContent();
	}

	bool PackagePropertyIterator::IsValid() const
	{
		return m_index < m_propertyTable.Size();
	}

	bool PackagePropertyIterator::IsObjectProperty() const
	{
		return m_type->GetType() == RT_Class;
	}

	PackagePropertyView PackagePropertyIterator::GetObjectPropertyView() const
	{
		RED_FATAL_ASSERT( IsObjectProperty(), "GetObjectPropertyView can only operate on Object property" );
		const PropertyDescriptor& descriptor = m_propertyTable[ m_index ];
		return PackagePropertyView( m_propertyData.Range( descriptor.dataOffset ), m_table );
	}

	void PackagePropertyIterator::ResolveContent()
	{
		if( IsValid() )
		{
			const PropertyDescriptor& descriptor = m_propertyTable[ m_index ];
			m_name = GetName( descriptor.nameIndex );
			const CName typeName = GetName( descriptor.typeNameIndex );
			m_type = GetRttiSystem().FindType( typeName );
			m_data = m_propertyData.Data( descriptor.dataOffset );
		}
	}

	CName PackagePropertyIterator::GetName( Uint32 tableIndex ) const
	{
		return m_table->UnmapName( tableIndex );
	}

	PackagePropertyView::PackagePropertyView( red::BlobView propertyData, PackageTableOfContent* table, Uint32 version )
		: m_propertyData( propertyData )
		, m_table( table )
		, m_version( version )
	{
		// ctremblay: If any change occur in PackageObjectSerializer, this code need to be fix most likely.
		// You can bump version of package, and handle different data structure.
		const Uint16 propertyCount = m_propertyData.As< Uint16 >();
		if( propertyCount )
		{
			m_propertyTable = PackagePropertyTable( m_propertyData.Pointer< const PropertyDescriptor >( sizeof( propertyCount ) ), propertyCount );
		}
	}

	PackagePropertyView::~PackagePropertyView()
	{}

	Uint32 PackagePropertyView::GetCount() const
	{
		return m_propertyTable.Size();
	}

	PackagePropertyIterator PackagePropertyView::GetPropertyIterator() const
	{
		return PackagePropertyIterator( m_propertyData, m_table, m_propertyTable, m_version );
	}

	PackageObjectIterator::PackageObjectIterator( const Package& package, Bool root /* = false */ )
		: m_package( &package )
		, m_iteratedTable( root ? &package.rootObjectTable : &package.objectTable )
		, m_toc( package )
		, m_index( 0 )
		, m_type( nullptr )
	{
		ResolveContent();
	}

	PackageObjectIterator::~PackageObjectIterator()
	{}

	const rtti::ClassType* PackageObjectIterator::GetType() const
	{
		return m_type;
	}

	PackagePropertyView PackageObjectIterator::GetPropertyView() const
	{
		const Uint32 offset = GetDataOffset();
		return PackagePropertyView( m_package->buffer.Range( offset ), &m_toc, m_package->version );
	}

	void PackageObjectIterator::Next()
	{
		++m_index;
		ResolveContent();
	}

	bool PackageObjectIterator::IsValid() const
	{
		return m_index < m_iteratedTable->Size();
	}

	bool PackageObjectIterator::IsTypeValid() const
	{
		return IsValid() && m_type != nullptr;
	}

	void PackageObjectIterator::ResolveContent()
	{
		if( IsValid() )
		{
			const ObjectDescriptor& descriptor = ( *m_iteratedTable )[ m_index ];
			m_typeName = GetName( descriptor.typeNameIndex );
			m_type = GetRttiSystem().FindClass( m_typeName );
		}
	}

	CName PackageObjectIterator::GetName( Uint32 tableIndex ) const
	{
		const StringDescriptor descriptor = m_package->stringTable[ tableIndex ];

		// FIXME cannot use descriptor.size as strings which exceed maximum length are being stored and overflowing size
		//const Uint8 size = static_cast< Uint8 >( descriptor.size ) - 1; // Cast to ensure -1 rolls over to 255 instead of 0xffffffff, as 0 size means string length was 255 originally and +1 was added for null terminator
		//const red::StringView name{ m_package->buffer.Pointer< char >( descriptor.offset ), size };
		const red::StringView name{ m_package->buffer.Pointer< char >( descriptor.offset ) };
		
		return RED_NAME( name );
	}

	CName PackageObjectIterator::GetTypeName() const
	{
		return m_typeName;
	}

	Uint32 PackageObjectIterator::GetDataOffset() const
	{
		return ( *m_iteratedTable )[ m_index ].dataOffset;
	}

	Uint32 PackageObjectIterator::GetDataSize() const
	{
		const Uint32 beginDataOffset = GetDataOffset();
		const Uint32 endDataOffset =
			m_index < m_iteratedTable->Size() - 1 ? ( *m_iteratedTable )[ m_index + 1 ].dataOffset : m_package->buffer.Size();

		return endDataOffset - beginDataOffset;
	}

	BlobView PackageObjectIterator::GetData() const
	{
		const Uint32 offset = GetDataOffset();
		const Uint32 size = GetDataSize();
		return m_package->buffer.Range( offset, size );
	}

	void PackageObjectIterator::SetCurrentIndex( Uint32 index )
	{
		m_index = index;
		ResolveContent();
	}

	Uint32 PackageObjectIterator::GetCurrentIndex() const
	{
		return m_index;
	}

	PackagePropertyIterator PackageObjectIterator::FindProperty( const CName& propertyName ) const
	{
		const PackagePropertyView view = GetPropertyView();
		PackagePropertyIterator iter = view.GetPropertyIterator();
		for( ; iter.IsValid(); iter.Next() )
		{
			if( iter.GetName() == propertyName )
			{
				break;
			}
		}

		return iter;
	}

	PackageResourceIterator::PackageResourceIterator()
		: m_package( nullptr )
		, m_index( 0 )
		, m_import( false )
		, m_resourcePathStringDiscarded( false )
	{		
	}

	PackageResourceIterator::PackageResourceIterator( const Package& package )
		: m_package( nullptr )
		, m_index( 0 )
		, m_import( false )
		, m_resourcePathStringDiscarded( false )
	{
		SetPackage( package );
	}

	PackageResourceIterator::~PackageResourceIterator()
	{
	}

	void PackageResourceIterator::SetPackage( const Package& package )
	{
		m_package = &package;

		if( IsValid() )
		{
			// ctremblay: Quick and dirty check if we saved hash instead of resource path.
			// This definitely should be more robust... But it is extremely unlikely we have only path of 8 character...
			// Just base folder + extension is already at a minimum of 6 character.

			const Uint32 pathCount = package.resourceTable.Size();
			const Uint32 start = package.resourceTable[ 0 ].offset;
			const Uint32 end = package.resourceTable.Back().offset + package.resourceTable.Back().size;
			const Uint32 tableSize = end - start;
			m_resourcePathStringDiscarded = ( tableSize == sizeof( Uint64 ) * pathCount );
		}

		SetIndex( 0 );
	}

	const res::ResourcePath& PackageResourceIterator::GetPath() const
	{
		return m_path;
	}

	Uint32 PackageResourceIterator::GetIndex() const
	{
		return m_index;
	}

	void PackageResourceIterator::SetIndex( Uint32 index )
	{
		m_index = index;
		ResolveResource();
	}

	PackageResourceImportType PackageResourceIterator::GetImportType() const
	{
		return m_import == false ? PackageResourceImportType::PackageResourceImportType_Async : PackageResourceImportType_Sync;
	}

	void PackageResourceIterator::Next()
	{
		++m_index;
		ResolveResource();
	}

	bool PackageResourceIterator::IsValid()
	{
		return m_package && m_index < m_package->resourceTable.Size();
	}

	void PackageResourceIterator::ResolveResource()
	{
		if( IsValid() )
		{
			const ResourceDescriptor descriptor = m_package->resourceTable[ m_index ];
			m_import = descriptor.import;
			if( !m_resourcePathStringDiscarded )
			{
				const red::StringView resourcePath{ m_package->buffer.Pointer< char >( descriptor.offset ), descriptor.size };
				m_path = res::ResourcePath::Build( resourcePath );
			}
			else
			{
				const Uint8* position = m_package->buffer.Pointer< Uint8 >( descriptor.offset );
				const Uint64 pathHash = *( reinterpret_cast< const Uint64* >( position ) );
				m_path = res::ResourcePath::Build( pathHash );
			}
		}
	}

	PackageNameIterator::PackageNameIterator( const Package& package )
		: m_package( &package )
		, m_index( 0 )
	{
		ResolveName();
	}

	PackageNameIterator::~PackageNameIterator()
	{
	}

	Uint32 PackageNameIterator::GetIndex() const
	{
		return m_index;
	}

	CName PackageNameIterator::GetName() const
	{
		return m_name;
	}

	void PackageNameIterator::Next()
	{
		++m_index;
		ResolveName();
	}

	bool PackageNameIterator::IsValid()
	{
		return m_index < m_package->stringTable.Size();
	}

	void PackageNameIterator::ResolveName()
	{
		if( IsValid() )
		{
			const auto& descriptor = m_package->stringTable[ m_index ];

			// FIXME cannot use descriptor.size as strings which exceed maximum length are being stored and overflowing size
			//const Uint8 size = static_cast< Uint8 >( descriptor.size ) - 1; // Cast to ensure -1 rolls over to 255 instead of 0xffffffff, as 0 size means string length was 255 originally and +1 was added for null terminator
			//const red::StringView name{ m_package->buffer.Pointer< char >( descriptor.offset ), size };
			const red::StringView name{ m_package->buffer.Pointer< char >( descriptor.offset ) };

			m_name = RED_NAME( name );
		}
	}

}
