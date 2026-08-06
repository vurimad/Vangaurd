/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "../../redContainers/include/fundamentalStringConversion.h"
#include "rttiType.h"
#include "rttiRegistration.h"
#include "rttiInternalTypeName.h"

namespace rtti
{
	/// General wrapper for type alignment
	template < typename T >
	struct TTypeAlignment
	{
		enum
		{
			Alignment = __alignof( T )
		};
	};

	/// Way to use simple types:
	///  in header file:
	//     RTTI_DECLARE_TYPE_NAME( Dupa );
	//   in cpp file:
	//     RTTI_DEFINE_SIMPLE_TYPE( Dupa );

	/// A user-defined SIMPLE rtti type
	/// Assumptions about T:
	///   type name was already defined via RTTI_DECLARE_TYPE_NAME
	///   has public constructor
	///   has non-virtual destructor
	///   has assignment operator
	///   has Serialize(IFile& file) method
	///   has global ::ToString, ::FromtString method implemented for type T
	template < class T  >
	class TSimpleType : public rtti::IType
	{
	public:
		virtual const CName GetName() const override final
		{
			return TTypeName< T >::GetTypeName(); // requires the RTTI_DECLARE_TYPE_NAME macro to be defined already
		}

		virtual ERTTITypeType GetType() const override final
		{
			return RT_Simple;
		}

		virtual Uint32 GetSize() const override final
		{
			return sizeof(T);
		}

		virtual Uint32 GetAlignment() const override final
		{
			return TTypeAlignment< T >::Alignment;
		}

		virtual void Construct( void* object ) const override final
		{
			::new (object) T();
		}

		virtual void Destruct( void* data ) const override final
		{
			((T*) data)->~T();
		}

		virtual Bool Compare( const void* data1, const void* data2, Uint32 /*flags*/ ) const override final
		{
			return *(const T*) data1 == *(const T*) data2;
		}

		virtual void Copy( void* dest, const void* src ) const override final
		{
			*(T*) dest = *(const T*) src;
		}

		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final
		{
			(*(T*)data).Serialize( file );
			return true;
		}

		virtual Bool ToString( const void* data, String& valueString ) const override final
		{
			return ToStringT( valueString, *(const T*) data );
		}

		virtual Bool FromString( void* data, const String& valueString ) const override final
		{
			return FromStringT( valueString, *(T*) data );
		}

		virtual Bool NeedsCleaning() const override final
		{		
			return !std::is_fundamental< T >::value;
		}

		virtual Bool IsTriviallyMovable() const override final
		{		
			return std::is_trivially_move_assignable< T >::value;
		}
	};

} // rtti

/// Implement the simple type wrapper, place this in C++
#define RTTI_DEFINE_SIMPLE_TYPE(_type) \
	void RegisterType##_type() { RTTIRegisterType( RED_NEW( rtti::TSimpleType<_type> ), GetNativeTypeHash<_type>() ); }

/// Implement the simple type wrapper, place this in C++
#define RTTI_DEFINE_SIMPLE_TYPE_ALIAS(_name, _type) \
	void RegisterType##_name() { RTTIRegisterType( RED_NEW( rtti::TSimpleType<_type> ), GetNativeTypeHash<_type>() ); }
