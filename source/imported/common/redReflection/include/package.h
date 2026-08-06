/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redContainers/include/blob.h"

#include "packageLayout.h"
#include "serializable.h"
#include "rttiProperty.h"

namespace red
{
	// ctremblay: IMPORTANT if you change anything here, you will break data. All the data.

	enum PackagePropertyFlags : Uint32
	{
		PackagePropertyType_Editable = PF_Editable,
		PackagePropertyType_Instanceable = PF_Instanceable,
		PackagePropertyType_Persistent = PF_Persistent,
		PackagePropertyType_All = ~0u
	};

	enum PackageResourceImportType : Int8
	{
		PackageResourceImportType_Async = 0,
		PackageResourceImportType_Sync,
	};

 	struct StringDescriptor
 	{
		enum { Type = PackageTableType_String };

 		Uint32 offset : 24;
 		Uint32 size : 8;
 	};

	struct PropertyDescriptor
	{
		Uint16 nameIndex;
		Uint16 typeNameIndex;
		Uint32 dataOffset;
	};

	struct ObjectDescriptor
	{
		enum { Type = PackageTableType_Object };

		Uint16 typeNameIndex;
		Uint16 padding;
		Uint32 dataOffset;
	};

	struct ResourceDescriptor
 	{
		enum { Type = PackageTableType_Resource };

 		Uint32 offset : 23;
 		Uint32 size : 8;
		Uint32 import : 1;
 	};

	struct Package
	{
		typedef red::ArraySpan< const StringDescriptor > StringTable;
		typedef red::ArraySpan< const ObjectDescriptor > ObjectTable;
		typedef red::ArraySpan< const ResourceDescriptor > ResourceTable;
		
		Uint32 version;

		ObjectTable rootObjectTable;
		ObjectTable objectTable;
		StringTable stringTable;
		ResourceTable resourceTable;

		red::BlobView buffer;
	};

	struct CompiledPackage
	{
		Package package;
		red::UniqueBuffer buffer;
	};
}
