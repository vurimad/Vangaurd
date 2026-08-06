/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiPointer.h"
#include "serializable.h"

namespace rtti
{
	Pointer TheNullPointer;

	Pointer& Pointer::Null()
	{
		return TheNullPointer;
	}

	Pointer::Pointer( ISerializable* object )
		: m_class( nullptr )
		, m_data( nullptr )
	{
		if ( NULL != object )
		{
			initialize( object, object->GetClass() );
		}
	}

	Bool Pointer::IsSerializable() const
	{
		return m_class && m_class->IsSerializable();
	}

	const rtti::ClassType* Pointer::GetRuntimeClass() const
	{
		if ( m_data )
		{
			// Use runtime class from serializable
			ISerializable* serializable = GetSerializablePtr();
			if ( NULL != serializable )
			{
				return serializable->GetClass();
			}

			// Use pointer class
			return m_class;
		}

		// no class known
		return NULL;
	}

	ISerializable* Pointer::GetSerializablePtr() const
	{
		if ( m_class && m_class->IsSerializable() )
		{
			return m_class->CastTo< ISerializable >( m_data );
		}

		return NULL;
	}

	void Pointer::initialize( void* ptr, const rtti::ClassType* ptrClass )
	{
		if ( m_data != ptr )
		{
			// Different pointer is being set
			m_class = ptrClass;
			m_data = ptr;
		}
		else
		{
			// Change the class only, pointer is the same
			m_class = ptrClass;
		}
	}

	void Pointer::release()
	{
		// Cleanup
		m_data = NULL;
		m_class = NULL;
	}

} // rtti