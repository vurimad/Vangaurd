/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "intrusiveList.h"
#include "assert.h"

namespace red
{
namespace memory
{
	IntrusiveSingleLinkedList::IntrusiveSingleLinkedList()
#ifdef RED_MEMORY_DETECT_SLAB_FREE_LIST_NULLPTR_STOMP
		: m_next( reinterpret_cast< IntrusiveSingleLinkedList* >( c_NextMask ) )
#else
		: m_next( nullptr )
#endif
	{}

	IntrusiveDoubleLinkedList::IntrusiveDoubleLinkedList()
		:	m_next( nullptr ),
			m_previous( nullptr )
	{
		m_next = this;
		m_previous = this;
	}

	void IntrusiveDoubleLinkedList::PushFront( IntrusiveDoubleLinkedList * node )
	{
		RED_MEMORY_ASSERT( node, "Node cannot be null." );

		IntrusiveDoubleLinkedList * currentNextNode = m_next;
		node->m_next = currentNextNode;
		node->m_previous = this;
		currentNextNode->m_previous = node;
		m_next = node;
	}

	void IntrusiveDoubleLinkedList::Remove()
	{
		IntrusiveDoubleLinkedList * previous = m_previous;
		IntrusiveDoubleLinkedList * next = m_next;
		next->m_previous = previous;
		previous->m_next = next;
		m_next = this;
		m_previous = this;
	}

	IntrusiveDoubleLinkedList * IntrusiveDoubleLinkedList::GetPrevious() const
	{
		return m_previous;
	}
}	
}
