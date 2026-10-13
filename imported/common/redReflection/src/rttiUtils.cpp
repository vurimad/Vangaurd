/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "rttiUtils.h"
#include "rttiSystem.h"
#include "rttiArrayTypesImpl.h"
#include "rttiPointerTypes.h"
#include "rttiClass.h"
#include "../../../common/redContainers/include/string/stringUtils.h"

using red::DynArray;

namespace rtti
{
	Uint32 GenerateUniqueTypeHash()
	{
		// Make hash generation return consecutive integers
		static red::Atomic< Uint32 > hashGenerator;
		return hashGenerator.Increment();
	}

	const Bool CanCast( const CName sourceType, const CName destType )
	{
		const auto* sourceRealType = GetRttiSystem().FindType( sourceType );
		const auto* destRealType = GetRttiSystem().FindType( destType );
		return CanCast( sourceRealType, destRealType );
	}

	const Bool CanCast( const rtti::IType* sourceType, const rtti::IType* destType )
	{
		// Source or destination type is invalid
		if ( !sourceType || !destType )
		{
			return false;
		}

		// The same type, casting obviously possible
		if ( sourceType == destType )
		{
			return true;
		}

		// We cannot cast between totally different types
		const ERTTITypeType sourceTypeType = sourceType->GetType();
		const ERTTITypeType destTypeType = destType->GetType();
		if ( sourceTypeType != destTypeType )
		{
			return false;
		}

		switch ( sourceTypeType )
		{
			// We can cast only between the same simple types
			case RT_Enum:
			case RT_Simple:
			case RT_Class:
			{
				// Compare types directly
				return sourceType->GetName() == destType->GetName();
			}

			// Arrays, recurse
			case RT_Array:
			{
				const auto* sourceArray = static_cast< const ArrayType* >( sourceType );
				const auto* destArray = static_cast< const ArrayType* >( destType );
				return CanCast( sourceArray->GetInnerType(), destArray->GetInnerType() );
			}

			// Arrays, recurse
			case RT_NativeArray:
			{
				const auto* sourceArray = static_cast< const NativeArrayType* >( sourceType );
				const auto* destArray = static_cast< const NativeArrayType* >( destType );
				return CanCast( sourceArray->GetInnerType(), destArray->GetInnerType() );
			}

			// Pointers/Handles, check classes
			case RT_Pointer:
			case RT_Handle:
			{
				const auto* sourcePointedType = static_cast< const IBasePointerType* >( sourceType )->GetPointedType();
				const auto* destPointedType = static_cast< const IBasePointerType* >( destType )->GetPointedType();
				if ( sourcePointedType && destPointedType )
				{
					if ( sourcePointedType->GetType() == RT_Class && destPointedType->GetType() == RT_Class )
					{
						const auto* sourceClass = static_cast< const rtti::ClassType* >( sourcePointedType );
						const auto* destClass = static_cast< const rtti::ClassType* >( destPointedType );

						// TODO: ehh, this is needed because of some stupid assertions in Brix
						// there is a lot of places when CNode* needs to be fitted inside a CEntity* type...
						if ( destClass->IsA( sourceClass ) )
						{
							return true;
						}

						return sourceClass->IsA( destClass );
					}
				}
			}

			// No casting possible
			default:
				return false;
		}
	}

	const CName FormatDynArrayTypeName( const CName innerTypeName )
	{
		RED_FATAL_ASSERT( innerTypeName, "Invalid inner type name" );

		static constexpr red::StringView prefix{ "array:" };
		const auto typeName = red::StrCat( prefix, innerTypeName.AsStringView() );

		return RED_NAME( typeName );
	}

	const CName FormatNativeArrayTypeName( const CName innerTypeName, const Uint32 elementCount )
	{
		RED_FATAL_ASSERT( elementCount >= 1, "Invalid element count" );
		RED_FATAL_ASSERT( innerTypeName, "Invalid inner type name" );

		AnsiChar typeName[ red::c_redCNameMaxLength ]; // what about really long names ?
		const auto length = red::SNPrintF( typeName, RED_ARRAY_COUNT(typeName), "[%u]%hs", elementCount, innerTypeName.AsChar() );

		return RED_NAME( red::StringView( typeName, length ) );
	}

	const CName FormatStaticArrayTypeName( const CName innerTypeName, const Uint32 maxSize )
	{
		RED_FATAL_ASSERT( innerTypeName, "Invalid inner type name" );

		AnsiChar typeName[ red::c_redCNameMaxLength ]; // what about really long names ?
		const auto length = red::SNPrintF( typeName, RED_ARRAY_COUNT(typeName), "static:%u,%hs", maxSize, innerTypeName.AsChar() );

		return RED_NAME( red::StringView( typeName, length ) );
	}

	const CName FormatScriptedReferenceTypeName( const CName pointedTypeName )
	{
		RED_FATAL_ASSERT( pointedTypeName, "Invalid name of the scripted reference type" );

		static constexpr red::StringView prefix = { "script_ref:" };
		const auto typeName = red::StrCat( prefix, pointedTypeName.AsStringView() );

		return RED_NAME( typeName );
	}

	const CName FormatPointerTypeName( const CName pointedTypeName )
	{
		RED_FATAL_ASSERT( pointedTypeName, "Invalid name of the pointed type" );

		static constexpr red::StringView prefix{ "ptr:" };
		const auto typeName = red::StrCat( prefix, pointedTypeName.AsStringView() );

		return RED_NAME( typeName );
	}

	const CName FormatHandleTypeName( const CName pointedTypeName )
	{
		RED_FATAL_ASSERT( pointedTypeName, "Invalid name of the handle type" );

		static constexpr red::StringView prefix{ "handle:" };
		const auto typeName = red::StrCat( prefix, pointedTypeName.AsStringView() );

		return RED_NAME( typeName );
	}

	const CName FormatWeakHandleTypeName( const CName pointedTypeName )
	{
		RED_FATAL_ASSERT( pointedTypeName, "Invalid name of the weak handle type" );

		static constexpr red::StringView prefix{ "whandle:" };
		const auto typeName = red::StrCat( prefix, pointedTypeName.AsStringView() );

		return RED_NAME( typeName );
	}

	const CName FormatResRefTypeName( const CName pointedTypeName )
	{
		RED_FATAL_ASSERT( pointedTypeName, "Invalid name of the handle type" );

		static constexpr red::StringView prefix{ "rRef:" };
		const auto typeName = red::StrCat( prefix, pointedTypeName.AsStringView() );

		return RED_NAME( typeName );
	}

	const CName FormatResAsyncRefTypeName( const CName pointedTypeName )
	{
		RED_FATAL_ASSERT( pointedTypeName, "Invalid name of the handle type" );

		static constexpr red::StringView prefix{ "raRef:" };
		const auto typeName = red::StrCat( prefix, pointedTypeName.AsStringView() );

		return RED_NAME( typeName );
	}

	const CName GetFilteredPropertyName( CName propName )
	{
		static constexpr red::StringView prefix{ "m_" };
		auto txt = propName.AsStringView();
		if ( txt.StartsWith( prefix ) )
		{
			txt.RemovePrefix( prefix.Length() );
			return RED_NAME( txt );
		}

		return propName;
	}

} // rtti
