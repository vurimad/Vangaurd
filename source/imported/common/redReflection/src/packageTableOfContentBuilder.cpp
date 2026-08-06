/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageTableOfContentBuilder.h"
#include "packageTable.h"
#include "packageBuilder.h"

namespace red
{
	PackageTableOfContentBuilder::PackageTableOfContentBuilder()
		: m_owner( nullptr )
		, m_table( nullptr )
		, m_currentObjectIndex(~0)
		, m_mappingCounter( 0 )
	{}

	PackageTableOfContentBuilder::~PackageTableOfContentBuilder()
	{}

	void PackageTableOfContentBuilder::Initialize( PackageBuilder * owner, PackageTable * table )
	{
		m_owner = owner;
		m_table = table;
	}
		 
	PackageTableOfContent::ObjectIndex PackageTableOfContentBuilder::MapRootObject( const ISerializable * object, const void * referenceObject )
	{
		m_currentObjectIndex = -1;
		const PackageTableOfContent::ObjectIndex index =  MapObject( object, referenceObject );
		auto iter = std::find( m_table->rootObjectTable.Begin(), m_table->rootObjectTable.End(), index );
		if( iter == m_table->rootObjectTable.End() )
		{
			m_table->rootObjectTable.PushBack( index );
			return m_table->rootObjectTable.Size() - 1;
		}

		return static_cast< Uint16 >( std::distance( m_table->rootObjectTable.Begin(), iter ) );
	}

	void PackageTableOfContentBuilder::OverrideRootObject( const ISerializable * object, const void * referenceObject, Uint32 index )
	{
		RED_FATAL_ASSERT(index < m_table->rootObjectTable.Size(), "There is no entry at this location.");

		++m_mappingCounter;
		const Int32 objectIndex = m_table->rootObjectTable[index];

		const SerializableID oldId = m_table->objectTable[objectIndex].objectId;
		if ( oldId.IsValid() )
			m_table->objectLookup.Remove( oldId );

		m_table->objectTable[objectIndex] = MakePackageTableObjectDescriptor( object, referenceObject );
		m_table->objectLookup[ m_table->objectTable[objectIndex].objectId ] = objectIndex;
		m_table->pendingObjectContainer.PushBack(objectIndex);
	}

	PackageTableObjectDescriptor PackageTableOfContentBuilder::MakePackageTableObjectDescriptor( const ISerializable * object, const void * referenceObject )
	{
		const SerializableID id = object->GetID();
		const rtti::ClassType * objectType = object->GetClass();
		const CName objectTypeName = objectType->GetName();

		const PackageTableObjectDescriptor descriptor =
		{
			id,
			object,
			referenceObject,
			MapName( objectTypeName ),
			0,
			0,
			m_mappingCounter
		};

		return descriptor;
	}

	void PackageTableOfContentBuilder::RemoveRootObject( Uint32 index, bool allowRecycling )
	{
		auto& rootObjectTable = m_table->rootObjectTable;
		RED_FATAL_ASSERT(index < rootObjectTable.Size(), "There is no entry at this location.");

		const Int32 objectIndex = rootObjectTable[index];
		const SerializableID oldId = m_table->objectTable[objectIndex].objectId;
		if ( oldId.IsValid() )
		{
			m_table->objectLookup.Remove( oldId );
		}

		m_table->objectTable[objectIndex] = InvalidPackageTableObjectDescriptor();

		// Some callers are maintaining an array that must maintain the same
		// ordering of the root object table. For now, allowRecycling will allow systems
		// to migrate to using a recycled-index-aware iterator (or whatever)
		if ( allowRecycling )
		{
			// Recycle the root object index instead of moving elements down
			if ( index < rootObjectTable.Size() - 1 )
			{
				rootObjectTable[index] = rootObjectTable[rootObjectTable.Size() - 1];
			}

			rootObjectTable.PopBack();
		}
		else
		{
			rootObjectTable.RemoveAt( index );
		}
	}

	PackageTableOfContent::ObjectIndex PackageTableOfContentBuilder::OnMapObject(const ISerializable * object, const void * referenceObject)
	{
		const SerializableID id = object->GetID();

		auto* ptr = m_table->objectLookup.FindPtr( id );
		if ( nullptr == ptr )
		{
			return DoMapObject( object, referenceObject );
		}

		const ObjectIndex position = *ptr;

		if( m_table->objectTable[ position ].mappingCounter != m_mappingCounter )
		{
			DoOverrideObject( object, referenceObject, position );	
		}

		return position;
	}

	PackageTableOfContent::ObjectIndex PackageTableOfContentBuilder::DoMapObject( const ISerializable * object, const void * referenceObject )
	{
		const PackageTableObjectDescriptor descriptor = MakePackageTableObjectDescriptor( object, referenceObject );
		m_table->objectTable.PushBack( descriptor );

		const Int32 pendingObjectId = m_table->objectTable.Size() - 1;
		m_table->objectLookup[ descriptor.objectId ] = pendingObjectId;
		m_table->pendingObjectContainer.PushBack( pendingObjectId );

		return pendingObjectId;
	}

	void PackageTableOfContentBuilder::DoOverrideObject( const ISerializable * object, const void * referenceObject, ObjectIndex position )
	{
		const PackageTableObjectDescriptor descriptor = MakePackageTableObjectDescriptor( object, referenceObject );
		const SerializableID oldId = m_table->objectTable[position].objectId;
		if ( oldId.IsValid() )
			m_table->objectLookup.Remove( oldId );

		m_table->objectTable[ position ] = descriptor;
		m_table->objectLookup[ descriptor.objectId ] = position;
		m_table->pendingObjectContainer.PushBack( position );
	}

	PackageTableOfContent::NameIndex PackageTableOfContentBuilder::OnMapName(CName name)
	{
		auto iter = m_table->stringLookup.Find( name );
		if( iter == m_table->stringLookup.End() )
		{
			m_table->stringTable.PushBack( name );
			auto index = m_table->stringTable.Size() - 1;
			m_table->stringLookup.Insert( name, index );
			return static_cast< NameIndex >( index );
		}

		return static_cast< NameIndex>( iter.Value() );
	}

	PackageTableOfContent::ResourceIndex PackageTableOfContentBuilder::OnMapResource( const res::ResourcePath & path, PackageResourceImportType importType )
	{
		auto predicate = [path]( const PackageTableResourceDescriptor & descriptor ) { return descriptor.path == path; };
		auto iter = std::find_if( m_table->resourceTable.Begin(), m_table->resourceTable.End(), predicate );
		if( iter == m_table->resourceTable.End() )
		{
			m_table->resourceTable.PushBack( { path, importType } );
			return m_table->resourceTable.Size() - 1;
		}

		iter->importType |= importType;
		return static_cast<Uint16>(std::distance(m_table->resourceTable.Begin(), iter));
	}

	CName PackageTableOfContentBuilder::OnUnmapName( NameIndex index ) const
	{
		if( index < m_table->stringTable.Size() )
		{
			return m_table->stringTable[ index ];
		}

		return CName();
	}

	void PackageTableOfContentBuilder::OnUnmapObject( ObjectIndex index, SerializableHandle & handle ) const
	{
		handle = m_owner->ReadObject( index );
	}

	res::ResourcePath PackageTableOfContentBuilder::OnUnmapResource( ResourceIndex index, res::ResourceTokenHandle & token ) const
	{
		if( index < m_table->resourceTable.Size() )
		{
			return m_table->resourceTable[ index ].path;
		}
		
		return res::ResourcePath();
	}

	PackageTableOfContent::ObjectIndex PackageTableOfContentBuilder::OnRemapObject( ObjectIndex index )
	{
		RED_FATAL("Cannot RemapObject when building package.");
		return ~0;
	}

	PackageTableOfContent::NameIndex PackageTableOfContentBuilder::OnRemapName( NameIndex )
	{
		RED_FATAL( "Cannot RemapName when building package." );
		return ~0;
	}

	PackageTableOfContent::ResourceIndex PackageTableOfContentBuilder::OnRemapResource( ResourceIndex, PackageResourceImportType  )
	{
		RED_FATAL( "Cannot RemapResource when building package." );
		return ~0;
	}

	void PackageTableOfContentBuilder::SetCurrentObjectIndex(Int32 index)
	{
		m_currentObjectIndex = index;
	}

}