/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packageTableOfContent.h"
#include "resourceLoaderTypes.h"
#include "resourceToken.h"
#include "packageIterator.h"

namespace red
{
	struct Package;

	struct PackageTableReader
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

		typedef red::DynArray< SerializableHandle > ObjectTable;
		typedef red::DynArray< res::ResourceTokenHandle > ResourceTable;
		typedef red::DynArray< Int32 > PendingObjectContainer;

		ObjectTable objectTable{ red::PoolEngine() };
		ResourceTable resourceTable{ red::PoolEngine() };
		PendingObjectContainer pendingObjectContainer { red::PoolEngine() };
	};

	class PackageTableOfContentReader : public PackageTableOfContent
	{
	public:

		PackageTableOfContentReader();
		virtual ~PackageTableOfContentReader();

		void Initialize( const Package & package, PackageTableReader & table );

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

		const Package * m_package;
		PackageTableReader* m_table;
		mutable PackageResourceIterator m_resourceIterator;
	};

	red::UniquePtr< PackageTableOfContentReader > CreatePackageTableOfContentReader( const Package & package, PackageTableReader & table  );
}

