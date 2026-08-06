/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

namespace red
{

RUID::RUID( RUID&& other )
	: m_value( other.m_value )
{
	other.m_value = 0;
}

RUID& RUID::operator=( RUID&& rhs )
{
	if (this != &rhs)
	{
		m_value = rhs.m_value;
		rhs.m_value = 0;
	}
	return *this;
}

Bool RUID::operator<( RUID rhs ) const
{
	return m_value < rhs.m_value;
}

Bool RUID::operator==( RUID rhs ) const
{
	return m_value == rhs.m_value;
}

Bool RUID::operator!=( RUID rhs ) const
{
	return !( *this == rhs );
}

Bool RUID::IsZero() const
{
	return m_value == 0;
}

Bool RUID::IsStatic() const
{
	return (m_value & (c_transientMask | c_virtualMask)) == 0;
}

Bool RUID::IsTransient() const
{
	return (m_value & c_transientMask) != 0;
}

Bool RUID::IsVirtual() const
{
	return (m_value & c_virtualMask) != 0;
}

RUID::RUID()
	: m_value( 0 )
{}

RUID::RUID( EUndefined )
{}

RUID::RUID( Uint64 value )
	: m_value( value )
{
	checkValue();
}

Uint32 RUID::CalcHash() const
{
	static_assert(sizeof(m_value) == sizeof(Uint64), "value should be 64-bit");
	return static_cast<Uint32>( (m_value >> 32) ^ m_value ); // drops upper bits in cast
}

void RUID::checkValue()
{
	constexpr Uint64 invalidMask = c_transientMask | c_virtualMask; // exclusive values
	RED_UNUSED( invalidMask );
	RED_FATAL_ASSERT( ( m_value & invalidMask ) != invalidMask, "Exclusive bits set in RUID!" );
	RED_FATAL_ASSERT( !IsVirtual() || s_isVirtualRUIDsEnabled, "Virtual RUIDs disabled" );
}

///---

RUIDRef::RUIDRef()
	: m_ruid()
{
}

RUIDRef::RUIDRef( RUID ruid )
	: m_ruid( Undefined )
{
	m_ruid.m_value = ruid.m_value;
}

RUIDRef::operator RUID() const
{
	return m_ruid;
}

Uint32 RUIDRef::CalcHash() const
{
	return m_ruid.CalcHash();
}

Bool RUIDRef::IsZero() const
{
	return m_ruid.IsZero();
}

Bool RUIDRef::IsStatic() const
{
	return m_ruid.IsStatic();
}

Bool RUIDRef::IsTransient() const
{
	return m_ruid.IsTransient();
}

Bool RUIDRef::IsVirtual() const
{
	return m_ruid.IsVirtual();
}

Bool RUIDRef::FromString( const char* str )
{
	return m_ruid.FromString( str );
}

void RUIDRef::ToString( char* buffer, Uint32 bufferSize, const char* customFormat /*= nullptr */ ) const
{
	return m_ruid.ToString( buffer, bufferSize, customFormat );
}

Bool operator==( RUIDRef lhs, RUIDRef rhs )
{
	return lhs.m_ruid == rhs.m_ruid;
}

Bool operator!=( RUIDRef lhs, RUIDRef rhs )
{
	return !( lhs == rhs );
}

Bool operator<( RUIDRef lhs, RUIDRef rhs )
{
	return lhs.m_ruid < rhs.m_ruid;
}

}
