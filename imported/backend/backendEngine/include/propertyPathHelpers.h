// Copyright (C) 2019 CD Projekt Red. All Rights Reserved.

#pragma once

namespace propertyPathHelpers
{
	using ExtractPropertyCallback = std::function< void( const void* obj, const rtti::IType* propertyType, const rtti::Property* property ) >;
	Bool BACKEND_ENGINE_API ExtractProperty( const void* obj, const rtti::IType* objType, const rtti::AccessPath& path, const ExtractPropertyCallback& callback );
	Bool BACKEND_ENGINE_API ExtractProperty( const ISerializable& serializable, const rtti::AccessPath& path, const ExtractPropertyCallback& callback );
} // namespace propertyPathHelpers
