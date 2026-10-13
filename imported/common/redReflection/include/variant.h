/**
* Copyright 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "rttiType.h"
#include "rttiUtils.h"
#include "handle.h"

namespace rtti
{

/// Variant variable - can hold any value supported by RTTI
class RED_REFLECTION_API Variant
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:

	//! Empty constructor
	RED_INLINE Variant();

	//! Construct with exact type and data
	RED_INLINE Variant( const rtti::IType* type, const void* data );

	//! Construct with type name and data
	RED_INLINE Variant( const CName& typeName, const void* data );

	//! Construct with typed value
	template < typename T >
	RED_INLINE explicit Variant( const T& value );
	
	//! Copy constructor
	RED_INLINE Variant( const Variant& other );

	//! Move constructor
	RED_INLINE Variant( Variant&& other );

	//! Cleanup
	RED_INLINE ~Variant();

	//! Copy assignment
	RED_INLINE Variant& operator=( const Variant& other );

	//! Move assignment
	RED_INLINE Variant& operator=( Variant&& other );

	//! Swap
	RED_INLINE void Swap( Variant& other );

	//! Is equal
	Bool operator==( const Variant& other ) const;
		
	//! Is not equal
	Bool operator!=( const Variant& other ) const;

	//! Get type
	RED_INLINE const rtti::IType* GetRTTIType() const { return GetTypeInternal(); }

	//! Get stored type name
	RED_INLINE CName GetTypeName() const;

	//! Get stored type size
	RED_INLINE Uint32 GetTypeSize() const;

	//! Is valid? ( has type defined )
	RED_INLINE Bool IsValid() const { return GetTypeInternal() != nullptr; }

	//! Get raw data
	RED_INLINE const void* GetData() const { return IsKeepingDataDirectly() ? m_dataDirect : m_data; }

	//! Is this variant an array ? ( arrays can be accessed via VariantArray ) 
	RED_INLINE Bool IsArray() const;

	//! Extract value using RTTI system, returns false if variant is of type different than specified
	template < class T >
	RED_INLINE Bool Get( T& value ) const;

	//! Extract value using RTTI system, causes fatal assert if variant is of type different than specified
	template < class T >
	RED_INLINE const T& Get() const;

	//! Set variant type and data
	void Set( const rtti::IType* type, const void* data );

	//! Set variant type and data
	void Set( CName typeName, const void* data );

	//! Set variant data
	template < typename T >
	RED_INLINE void Set( const T& value );

	//! Set variant value without changing its type
	template < typename T >
	RED_INLINE void SetValue( const T& value );

	//! Destroy variant value
	void Clear();

	//! Convert value to string, returns false on error
	red::String ValueToString() const;

	//! Convert value to string, returns false on error
	Bool ValueToString( red::String& value ) const;

	//! Set value from string, returns false on error
	Bool ValueFromString( const char* value );

	//! Set value from string, returns false on error
	Bool ValueFromString( const red::String& value );

	//! Serialization
	void Serialize( IFile& file );

	//! Direct serialization of stored data, returns false on error
	Bool SerializeData( IFile& file );

	//! Serialization operator
	RED_INLINE friend void operator<<( IFile& file, Variant& variant )
	{
		variant.Serialize( file );
	}

	void* Internal_GetData(); // ctremblay: DO NOT USE. This is temprorary for porting Variant to Package.

private:

	enum : Uint64
	{
		FLAG_KEEP_DATA_DIRECTLY		= RED_FLAG( 0 ),
		MASK_POINTER				= ~FLAG_KEEP_DATA_DIRECTLY,
		DIRECT_DATA_SIZE			= sizeof( THandle< ISerializable > ),
	};

	const rtti::IType*	m_type;							//<! RTTI type used to do all the stuff. The last 3 bits are always 0 (cause alignof( rtti::IType ) >= 8). We'll keep flags there.
	union
	{
		void*		m_data;								//!< Data is allocated via the heap
		Uint8		m_dataDirect[ DIRECT_DATA_SIZE ];	//!< Data that are stored directly in Variant
	};

	RED_INLINE Bool IsKeepingDataDirectly() const { return ( reinterpret_cast< uintptr_t >( m_type ) & FLAG_KEEP_DATA_DIRECTLY ) != 0; }
	RED_INLINE const rtti::IType* GetTypeInternal() const { return ( reinterpret_cast< const rtti::IType* >( reinterpret_cast< uintptr_t >( m_type ) & MASK_POINTER ) ); }
	
	void ChangeType( const rtti::IType* newType, const void* data );
	RED_INLINE void SetValueInternal( const void* data );
	void* InitDataAndFlag();
	void FreeData();

	static_assert( ( FLAG_KEEP_DATA_DIRECTLY & MASK_POINTER ) == 0, "Flags cannot overlap with MASK_POINTER" );
};

} // rtti

template <typename T>
struct TTypeName;

 // TTypeName::GetTypeName name is declared here directly instead of using macro, to keep "old" type name ("Variant" instead of "rttiVariant" or "rtti::Variant")
template <>
struct TTypeName< rtti::Variant >
{
	static const CName GetTypeName()
	{
		static CName theName = RED_NAME_CONSTEXPR( "Variant" );
		return theName;
	}
};

#include "variant.hpp"