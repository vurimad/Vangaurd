/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packageTableOfContent.h"

namespace red
{
	class PackageTableOfContentView;

	struct PackageRemapTable
	{
		struct PackageRemapObjectIndex
		{
			PackageTableOfContent::ObjectIndex oldIndex;
			PackageTableOfContent::ObjectIndex newIndex;
		};

		typedef red::HashMap< PackageTableOfContent::ObjectIndex, PackageTableOfContent::ObjectIndex > RemapTable;
		typedef red::DynArray< PackageRemapObjectIndex > PendingRemapContainer;

		PackageTableOfContent::ObjectIndex nextIndex;
		RemapTable remappingTable{ red::PoolEngine() };
		PendingRemapContainer pendingRemapping{ red::PoolEngine() };
	};

	struct PackageTableOfContentRemapParameter
	{
		const PackageTableOfContentView * inputTable;
		PackageTableOfContent * outputTable;
		PackageRemapTable * table;
	};

	class PackageTableOfContentRemap : public PackageTableOfContent
	{
	public:
		PackageTableOfContentRemap();
		virtual ~PackageTableOfContentRemap();

		void Initialize( const PackageTableOfContentRemapParameter& param );

	private:

		virtual NameIndex OnMapName( CName name ) override final;
		virtual ObjectIndex OnMapObject( const ISerializable * object, const void * referenceObject ) override final;
		virtual ResourceIndex OnMapResource( const res::ResourcePath & path, PackageResourceImportType importType ) override final;

		virtual CName OnUnmapName( NameIndex index ) const override final;
		virtual void OnUnmapObject( ObjectIndex index, SerializableHandle & handle ) const override final;
		virtual res::ResourcePath OnUnmapResource( ResourceIndex index, res::ResourceTokenHandle & token ) const override final;

		virtual ObjectIndex OnRemapObject( ObjectIndex index ) override final;
		virtual NameIndex OnRemapName( NameIndex ) override final;
		virtual ResourceIndex OnRemapResource( ResourceIndex, PackageResourceImportType importType ) override final;

		const PackageTableOfContentView * m_inputTable;
		PackageTableOfContent * m_outputTable;
		PackageRemapTable * m_table;
	};

	UniquePtr< PackageTableOfContentRemap > CreatePackageTableOfContentRemap( const PackageTableOfContentRemapParameter & param );
}
