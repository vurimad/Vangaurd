/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "handle.h"

namespace red
{
	enum PackageResourceImportType : Int8;

	struct PackageTableObjectDescriptor
	{
		SerializableID objectId;
		const ISerializable* object; 
		const void * referenceObject;
		Uint16 nameIndex;
		Uint32 propertyDataOffset;
		Uint32 propertyDataSize;
		Uint32 mappingCounter;
	};

	struct PackageTableResourceDescriptor
	{
		res::ResourcePath path;
		Int8 importType;
	};

	PackageTableObjectDescriptor InvalidPackageTableObjectDescriptor();

	bool operator==(const PackageTableObjectDescriptor & left, const PackageTableObjectDescriptor & right);

	struct PackageTable
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

		typedef red::DynArray< CName > StringTable;
		typedef red::HashMap< CName, Uint32 > StringLookup;
		typedef red::DynArray< StringView > StringViewTable;
		typedef red::DynArray< PackageTableObjectDescriptor > ObjectTable;
		typedef red::HashMap< SerializableID, Uint32 > ObjectLookup;
		typedef red::DynArray< Int32 > RootObjectTable;
		typedef red::DynArray< PackageTableResourceDescriptor > ResourceTable;
		typedef red::DynArray< Int32 > PendingObjectContainer;
		
		
		ResourceTable resourceTable { red::PoolEngine() };
		StringTable stringTable { red::PoolEngine() };
		StringLookup stringLookup { red::PoolEngine() };
		RootObjectTable rootObjectTable { red::PoolEngine() };
		ObjectTable objectTable { red::PoolEngine() };
		ObjectLookup objectLookup { red::PoolEngine() };
		PendingObjectContainer pendingObjectContainer { red::PoolEngine() };
		StringViewTable stringViewTable{ red::PoolEngine() };
	};

	Uint32 ComputePackageDataSize( const PackageTable & table, red::DynArray< red::StringView >& outputNames, bool discardResourcePathString );
};
