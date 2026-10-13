/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "selectionIterator.h"
#include "selectionState.h"

namespace tools
{
	SelectionIterator::SelectionIterator( const CName typeName, const SelectionState* selectionState)
		: m_type( typeName )
		, m_current( 0 )
		, m_count( 0 )
		, m_objects( selectionState ) // TODO: extend the SharedPtr
	{
		if ( m_objects )
			m_count = m_objects->m_proxyList.Size();
	}

	SelectionIterator::SelectionIterator( const SelectionIterator& other )
		: m_type( other.m_type )
		, m_count( other.m_count )
		, m_current( other.m_current )
		, m_objects( other.m_objects )
	{
	}

	SelectionIterator::~SelectionIterator()
	{
	}

	const EditableProxyPtr SelectionIterator::GetCurrentPtr() const
	{
		if ( m_objects )
		{
			if ( m_current < m_objects->m_proxyList.Size() )
			{
				const auto proxyPtr = m_objects->m_proxyList[ m_current ];
				return proxyPtr;
			}
		}

		return EditableProxyPtr();
	}

	EditableProxyPtr SelectionIterator::GetCurrentPtr()
	{
		return *const_cast<EditableProxyPtr*>( &static_cast<const SelectionIterator*>( this )->GetCurrentPtr() );
	}

	const IEditableProxy* SelectionIterator::GetCurrent() const
	{
		EditableProxyPtr ptr = GetCurrentPtr();

		return ptr.Get();
	}

	IEditableProxy* SelectionIterator::GetCurrent()
	{
		return const_cast<IEditableProxy*>( static_cast<const SelectionIterator*>( this )->GetCurrent() );
	}

} // tools