/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "selectionFwd.h"
#include "editableFwd.h"

namespace tools
{

	/// selection iterator for given type
	/// allows to iterate over selection proxies (any only that)
	class BACKEND_ENGINE_API SelectionIterator : red::NonCopyable
	{
	public:
		SelectionIterator( const CName typeName, const SelectionState* selectionState );
		SelectionIterator( const SelectionIterator& other );
		~SelectionIterator();

		// is valid ?
		RED_FORCE_INLINE explicit operator Bool() const
		{
			return m_current < m_count; 
		}

		// get the proxy
		RED_FORCE_INLINE const IEditableProxy* operator->() const
		{
			RED_FATAL_ASSERT( m_current < m_count, "Invalid selection iterator usage" );
			return GetCurrent();
		}

		// get the proxy
		RED_FORCE_INLINE IEditableProxy* operator->()
		{
			RED_FATAL_ASSERT( m_current < m_count, "Invalid selection iterator usage" );
			return GetCurrent();
		}

		// advance
		RED_FORCE_INLINE void operator++()
		{
			m_current += 1;
		}

		// copy
		RED_FORCE_INLINE SelectionIterator& operator=( const SelectionIterator& other )
		{
			if ( this != &other )
			{
				m_objects = other.m_objects;
				m_type = other.m_type;
				m_count = other.m_count;
				m_current = other.m_current;
			}

			return *this;
		}

		// Get the shared pointer to the iteration object
		const EditableProxyPtr GetCurrentPtr() const;
		EditableProxyPtr GetCurrentPtr();

	private:
		const SelectionState*			m_objects;

		CName							m_type;
		Uint32							m_current;
		Uint32							m_count;

		const IEditableProxy* GetCurrent() const;
		IEditableProxy* GetCurrent();
	};

} // tools