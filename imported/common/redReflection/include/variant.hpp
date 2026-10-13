/**
* Copyright 2007-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiRegistration.h"
#include "rttiSystem.h"
#include "rttiTypeName.h"

namespace rtti
{

RED_INLINE Variant::Variant()
	: m_type( nullptr )
	, m_dataDirect{}
{
}

RED_INLINE Variant::Variant( const rtti::IType* type, const void* data )
	: m_type( type )
	, m_dataDirect{}
{
	void* myData = InitDataAndFlag();
	if ( type != nullptr && data != nullptr )
	{
		type->Copy( myData, data );
	}
}

RED_INLINE Variant::Variant( const CName& typeName, const void* data )
	: Variant( GetRttiSystem().FindType( typeName ), data )
{
}

template < typename T >
RED_INLINE Variant::Variant( const T& value )
	: Variant( ::GetTypeObject< T >(), &value )
{
}

RED_INLINE Variant::Variant( const Variant& other )
	: Variant( other.GetTypeInternal(), other.GetData() )
{
}

RED_INLINE Variant::Variant( Variant&& other )
	: m_type( other.m_type )
{
	red::Memcpy( m_dataDirect, other.m_dataDirect, DIRECT_DATA_SIZE );
	other.m_type = nullptr;
	red::Memzero( other.m_dataDirect, DIRECT_DATA_SIZE );
}

RED_INLINE Variant::~Variant()
{	
	Clear();
}

//////////////////////////////////////////////////////////////////////////

RED_INLINE Variant& Variant::operator=( const Variant& other )
{
	if ( this != &other )
	{
		Set( other.GetTypeInternal(), other.GetData() );
	}
	return *this;
}

RED_INLINE Variant& Variant::operator=( Variant&& other )
{
	Variant( std::move( other ) ).Swap( *this );
	return *this;
}

RED_INLINE void Variant::Swap( Variant& other )
{
	using std::swap;
	swap( m_type, other.m_type );
	swap( m_dataDirect, other.m_dataDirect );
}

//////////////////////////////////////////////////////////////////////////

RED_INLINE CName Variant::GetTypeName() const
{
	const rtti::IType* myType = GetTypeInternal();
	return myType != nullptr ? myType->GetName() : CName::NONE();
}

RED_INLINE Uint32 Variant::GetTypeSize() const
{
	const rtti::IType* myType = GetTypeInternal();
	return myType != nullptr ? myType->GetSize() : 0;
}

RED_INLINE Bool Variant::IsArray() const
{
	const rtti::IType* myType = GetTypeInternal();
	// this only returns true for dynamic arrays!
	return myType != nullptr && myType->GetType() == RT_Array;
}

//////////////////////////////////////////////////////////////////////////

template < class T >
RED_INLINE Bool Variant::Get( T& value ) const
{
	// Check type and extract value
	const rtti::IType* targetType = GetTypeObject< T >();
	const rtti::IType* myType = GetTypeInternal();
	if ( rtti::CanCast( myType, targetType ) )
	{
		RED_ASSERT( myType->GetSize() == sizeof( T ) );
		value = *static_cast< const T* >( GetData() );
		return true;
	}
	// Not valid type
	return false;
}

template < class T >
RED_INLINE const T& Variant::Get() const
{
	// Check type and extract value
	const rtti::IType* targetType = GetTypeObject< T >();
	const rtti::IType* myType = GetTypeInternal();
	if ( rtti::CanCast( myType, targetType ) )
	{
		RED_ASSERT( myType->GetSize() == sizeof( T ) );
		return *static_cast< const T* >( GetData() );
	}
	// Not valid type
	RED_FATAL_ASSERT( false, "Cannot cast value stored in variant to specified type" );
	// The following line isn't correct but we need to returns something so that the code compiles
	return *static_cast< const T* >( GetData() );
}

template < typename T >
RED_INLINE void Variant::Set( const T& value )
{
	Set( ::GetTypeName< T >(), &value );
}

template < typename T >
RED_INLINE void Variant::SetValue( const T& value )
{
	const rtti::IType* myType = GetTypeInternal();
	RED_FATAL_ASSERT( myType != nullptr, "Cannot set value for variant with no type" );
	RED_FATAL_ASSERT( myType->GetName() == ::GetTypeName< T >(), "Specified value and variant have different types" );
	myType->Copy( Internal_GetData(), &value );
}

RED_INLINE void Variant::SetValueInternal( const void* data )
{
	const rtti::IType* myType = GetTypeInternal();
	if ( myType != nullptr && data != nullptr )
	{
		myType->Copy( Internal_GetData(), data );
	}
}

} // rtti