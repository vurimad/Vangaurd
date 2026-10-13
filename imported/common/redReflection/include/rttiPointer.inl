/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

namespace rtti
{

	RED_INLINE Pointer::Pointer()
		: m_data( nullptr )
		, m_class( nullptr )
	{
	}

	RED_INLINE Pointer::~Pointer()
	{
		release();
	}

	RED_INLINE Pointer::Pointer( void* pointer, const rtti::ClassType* theClass )
		: m_data( nullptr )
		, m_class( nullptr )
	{
		if ( NULL != pointer )
		{
			initialize( pointer, theClass );
		}
	}

	RED_INLINE Pointer::Pointer( const Pointer& pointer )
		: m_data( nullptr )
		, m_class( nullptr )
	{
		initialize( pointer.m_data, pointer.m_class );
	}

	RED_INLINE Pointer& Pointer::operator=( const Pointer& other )
	{
		initialize( other.m_data, other.m_class );
		return *this;
	}
	 
	RED_INLINE Bool Pointer::operator==( const Pointer& other ) const
	{
		// only the data pointers are compared directly
		return m_data == other.m_data;
	}

	RED_INLINE Bool Pointer::operator!=( const Pointer& other ) const
	{
		// only the data pointers are compared directly
		return m_data != other.m_data;
	}

} // rtti