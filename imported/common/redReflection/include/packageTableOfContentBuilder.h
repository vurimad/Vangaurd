/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packageTableOfContent.h"
#include "packageTable.h"

namespace red
{
	struct PackageTable;
	class PackageBuilder;

	class PackageTableOfContentBuilder : public PackageTableOfContent
	{
	public:

		PackageTableOfContentBuilder();
		virtual ~PackageTableOfContentBuilder();

		void Initialize( PackageBuilder * owner, PackageTable * table );

		ObjectIndex MapRootObject( const ISerializable * object, const void * referenceObject );
		void OverrideRootObject( const ISerializable * handle, const void * referenceObject, Uint32 index );
		void RemoveRootObject( Uint32 index, bool allowRecycling = false );

		void SetCurrentObjectIndex( Int32 index );

	private:

		virtual NameIndex OnMapName( CName name ) override final;
		virtual ObjectIndex OnMapObject( const ISerializable * object, const void * referenceObject ) override final;
		virtual ResourceIndex OnMapResource( const res::ResourcePath & path, PackageResourceImportType importType ) override final;

		virtual CName OnUnmapName( NameIndex index ) const override final;
		virtual void OnUnmapObject( ObjectIndex index, SerializableHandle & handle ) const override final;
		virtual res::ResourcePath OnUnmapResource( ResourceIndex index, res::ResourceTokenHandle & token ) const override final;

		virtual NameIndex OnRemapName( NameIndex ) override final;
		virtual ResourceIndex OnRemapResource( ResourceIndex, PackageResourceImportType ) override final;
		virtual ObjectIndex OnRemapObject( ObjectIndex index ) override final;

		ObjectIndex DoMapObject( const ISerializable * object, const void * referenceObject );
		void DoOverrideObject( const ISerializable * object, const void * referenceObject, ObjectIndex position );

		PackageTableObjectDescriptor MakePackageTableObjectDescriptor( const ISerializable * object, const void * referenceObject );

		PackageBuilder * m_owner;
		PackageTable * m_table;
		Int32 m_currentObjectIndex;
		Uint32 m_mappingCounter;
	};
}

