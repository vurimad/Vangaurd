/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageTableOfContentRemap.h"
#include "packageTableOfContentView.h"
#include "resourceToken.h"


namespace red
{
	PackageTableOfContentRemap::PackageTableOfContentRemap()
		: m_inputTable( nullptr )
		, m_outputTable( nullptr )
		, m_table( nullptr )
	{}

	PackageTableOfContentRemap::~PackageTableOfContentRemap()
	{}

	void PackageTableOfContentRemap::Initialize( const PackageTableOfContentRemapParameter& param )
	{
		m_inputTable = param.inputTable;
		m_outputTable = param.outputTable;
		m_table = param.table;
	}

	PackageTableOfContent::NameIndex PackageTableOfContentRemap::OnMapName( CName name )
	{
		return m_outputTable->MapName( name );
	}

	PackageTableOfContent::ObjectIndex PackageTableOfContentRemap::OnMapObject( const ISerializable* object, const void* referenceObject )
	{
		return m_outputTable->MapObject( object, referenceObject );
	}

	PackageTableOfContent::ResourceIndex PackageTableOfContentRemap::OnMapResource( const res::ResourcePath& path, PackageResourceImportType importType )
	{
		return m_outputTable->MapResource( path, importType );
	}

	CName PackageTableOfContentRemap::OnUnmapName( PackageTableOfContent::NameIndex index ) const
	{
		return m_inputTable->UnmapName( index );
	}

	void PackageTableOfContentRemap::OnUnmapObject( PackageTableOfContent::ObjectIndex index, SerializableHandle& handle ) const
	{
		return m_inputTable->UnmapObject( index, handle );
	}

	res::ResourcePath PackageTableOfContentRemap::OnUnmapResource( ResourceIndex index, res::ResourceTokenHandle& token ) const
	{
		return m_inputTable->UnmapResource( index, token );
	}

	PackageTableOfContent::ObjectIndex PackageTableOfContentRemap::OnRemapObject( ObjectIndex index )
	{
		auto& table = m_table->remappingTable;

		const auto iter = table.Find( index );
		if( iter != table.End() )
		{
			return iter.Value();
		}

		if( m_inputTable->IsObjectTypeValid( index ) )
		{
			const PackageTableOfContent::ObjectIndex newIndex = m_table->nextIndex++;
			const PackageRemapTable::PackageRemapObjectIndex remapIndex = { index, newIndex };
			table[ index ] = ( newIndex );
			m_table->pendingRemapping.PushBack( remapIndex );

			return newIndex;
		}

		return c_invalidObjectIndex;
	}

	PackageTableOfContent::NameIndex PackageTableOfContentRemap::OnRemapName( NameIndex index )
	{
		const CName inputName = m_inputTable->UnmapName( index );
		return m_outputTable->MapName( inputName );
	}

	PackageTableOfContent::ResourceIndex PackageTableOfContentRemap::OnRemapResource( ResourceIndex index, PackageResourceImportType importType )
	{
		res::ResourceTokenHandle token;
		const res::ResourcePath inputPath = m_inputTable->UnmapResource( index, token );
		return m_outputTable->MapResource( inputPath, importType );
	}

	UniquePtr< PackageTableOfContentRemap > CreatePackageTableOfContentRemap( const PackageTableOfContentRemapParameter& param )
	{
		UniquePtr< PackageTableOfContentRemap > tableOfContent = red::CreateUniquePtr< PackageTableOfContentRemap >();
		tableOfContent->Initialize( param );
		return tableOfContent;
	}
}
