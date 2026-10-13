/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiClassDeclarationMacros.h"
#include "rttiTypeName.h"

#include "../../redCore/include/absolutePath.h"

struct RED_REFLECTION_API AbsolutePathSerializable : public red::AbsolutePath
{
	RTTI_DECLARE_TYPE( AbsolutePathSerializable );

	AbsolutePathSerializable() = default;

	AbsolutePathSerializable( red::AbsolutePath&& absolutePath )
		: red::AbsolutePath( std::move( absolutePath ) )
	{}

	AbsolutePathSerializable( const red::AbsolutePath& absolutePath )
		: red::AbsolutePath( absolutePath )
	{}
};

template <> struct TCopyableType<AbsolutePathSerializable> { enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<red::AbsolutePath>()
{
	return GetTypeName<AbsolutePathSerializable>();
}

void RegisterFileTypeAliases();

