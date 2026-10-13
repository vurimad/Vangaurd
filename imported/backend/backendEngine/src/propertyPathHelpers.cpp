// Copyright (C) 2019 CD Projekt Red. All Rights Reserved.

#include "build.h"

#include "propertyPathHelpers.h"

#include "../../../common/redReflection/include/rttiPathParser.h"

namespace propertyPathHelpers
{
	Bool Internal_ExtractProperty( const void* obj, const rtti::IType* objType, const rtti::AccessPath& path, const rtti::Property* property, const ExtractPropertyCallback& callback )
	{
		rtti::PathParser parser{ path };
		if( !parser )
		{
			callback( obj, objType, property );
			return true;
		}

		switch( objType->GetType() )
		{
		case ERTTITypeType::RT_Handle:
		case ERTTITypeType::RT_WeakHandle:
		{
			const auto* pointerType = static_cast<const rtti::IBasePointerType*>(objType);
			const ISerializable* object = reinterpret_cast<const ISerializable*>(pointerType->GetPointer( obj ).GetPointer());
			if( object )
			{
				return Internal_ExtractProperty( object, object->GetClass(), path, property, callback );
			}
			break;
		}
		case ERTTITypeType::RT_Class:
		{
			CName name;
			if( parser.EatName( name ) )
			{
				const rtti::ClassType* classType = static_cast<const rtti::ClassType*>(objType);
				const rtti::Property* innerProperty = classType->FindProperty( name );
				if( innerProperty )
				{
					return Internal_ExtractProperty( innerProperty->GetOffsetPtr( obj ), innerProperty->GetType(), parser.GetUneatenPath(), innerProperty, callback );
				}
			}
			break;
		}
		case ERTTITypeType::RT_Array:
		case ERTTITypeType::RT_NativeArray:
		case ERTTITypeType::RT_StaticArray:
		{
			Int32 idx = 0;
			if( parser.EatIndex( idx ) )
			{
				const rtti::IBaseArrayType* arrayType = static_cast<const rtti::IBaseArrayType*>(objType);
				if( const void* element = arrayType->ArrayGetArrayElement( obj, static_cast<Uint32>(idx) ) )
				{
					return Internal_ExtractProperty( element, arrayType->ArrayGetInnerType(), parser.GetUneatenPath(), property, callback );
				}
			}
			break;
		}
		default:
			RED_FATAL( "Invalid property path." );
			break;
		}

		return false;
	}

	Bool ExtractProperty( const void* obj, const rtti::IType* objType, const rtti::AccessPath& path, const ExtractPropertyCallback& callback )
	{
		return Internal_ExtractProperty( obj, objType, path, nullptr, callback );
	}

	Bool ExtractProperty( const ISerializable& serializable, const rtti::AccessPath& path, const ExtractPropertyCallback& callback )
	{
		return ExtractProperty( &serializable, serializable.GetClass(), path, callback );
	}
}
