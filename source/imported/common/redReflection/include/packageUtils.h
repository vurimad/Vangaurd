/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "serializable.h"
#include "package.h"
#include "../../redMemory/include/uniqueBuffer.h"

namespace red
{
	struct GeneratePackageParameter
	{
		ISerializable * rootObject;
		ISerializable * defaultObject;
		PackagePropertyFlags flags;
	};

	RED_REFLECTION_API CompiledPackage GeneratePackage( const ISerializable * rootObject );
	RED_REFLECTION_API CompiledPackage GeneratePackage( const GeneratePackageParameter & parameter );
	
	RED_REFLECTION_API CompiledPackage MergePackage( const Package & left, const Package & right );
	RED_REFLECTION_API CompiledPackage RemovePackageEntry( const Package & package, Uint32 entry );

	RED_REFLECTION_API SerializableHandle CreateObject( const Package & package, Uint32 index );
	RED_REFLECTION_API SerializableHandle CreateObject( CName objectName );

	RED_REFLECTION_API void ApplyPackageContent( const Package & package, ISerializable & object, Uint32 index );
}
