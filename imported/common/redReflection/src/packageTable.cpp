/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageTable.h"
#include "package.h"

namespace red
{
	PackageTableObjectDescriptor InvalidPackageTableObjectDescriptor()
	{
		return { SerializableID(), nullptr, nullptr, (Uint16)~0u, ~0u, ~0u };
	}

	bool operator==(const PackageTableObjectDescriptor & left, const PackageTableObjectDescriptor & right)
	{
		return left.objectId == right.objectId
			&& left.referenceObject == right.referenceObject
			&& left.nameIndex == right.nameIndex
			&& left.propertyDataOffset == right.propertyDataOffset
			&& left.propertyDataSize == right.propertyDataSize;
	}

	Uint32 ComputePackageDataSize( const PackageTable & table, red::DynArray< red::StringView >& outputNames, bool discardResourcePathString  )
	{
		Uint32 totalResourceDataSize = sizeof( ResourceDescriptor ) * table.resourceTable.Size(); 
		Uint32 totalStringDataSize = sizeof( StringDescriptor ) * table.stringTable.Size(); 
		Uint32 totalObjectDataSize = sizeof( ObjectDescriptor ) * table.objectTable.Size(); 

		outputNames.Reserve( table.stringTable.Size() );

		if( discardResourcePathString )
		{ 
			totalResourceDataSize += table.resourceTable.Size() * sizeof( Uint64 );
		}
		else
		{
			for( Uint32 index = 0, end = table.resourceTable.Size(); index != end; ++index )
			{
				const PackageTableResourceDescriptor& descriptor = table.resourceTable[ index ];
				const res::ResourcePath& path = descriptor.path;
				const red::StringView pathView = path.ToStringView();
				const Uint32 size = pathView.Length();
				totalResourceDataSize += size;
			}
		}
		

		for( Uint32 index = 0, end = table.stringTable.Size(); index != end; ++index )
		{
			const auto str =  table.stringTable[ index ].AsStringView();
			const Uint32 size = str.Length() + 1;
			totalStringDataSize += size;
			outputNames.PushBack( str );
		}

		for( Uint32 index = 0, end = table.objectTable.Size(); index != end; ++index )
		{
			const PackageTableObjectDescriptor & descriptor = table.objectTable[ index ];
			const Uint32 size = descriptor.propertyDataSize;
			totalObjectDataSize += size;
		}

		return totalResourceDataSize + totalStringDataSize + totalObjectDataSize;
	}
}