/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_UTILITY_INTRUSIVE_LIST_HPP_
#define _RED_MEMORY_UTILITY_INTRUSIVE_LIST_HPP_

namespace red
{
namespace memory
{
	RED_MEMORY_INLINE IntrusiveSingleLinkedList * IntrusiveSingleLinkedList::GetNext() const
	{
#ifdef RED_MEMORY_DETECT_SLAB_FREE_LIST_NULLPTR_STOMP
		return reinterpret_cast< IntrusiveSingleLinkedList * >( reinterpret_cast< u64 >( m_next ) ^ c_NextMask );
#else
		return m_next;
#endif
	}

	RED_MEMORY_INLINE void IntrusiveSingleLinkedList::SetNext( IntrusiveSingleLinkedList * next )
	{
#ifdef RED_MEMORY_DETECT_SLAB_FREE_LIST_NULLPTR_STOMP
		m_next = reinterpret_cast< IntrusiveSingleLinkedList * >( reinterpret_cast< u64 >( next ) ^ c_NextMask );
#else
		m_next = next;
#endif
	}

	RED_MEMORY_INLINE IntrusiveDoubleLinkedList * IntrusiveDoubleLinkedList::GetNext() const
	{
		return m_next;
	}

	RED_MEMORY_INLINE bool IntrusiveDoubleLinkedList::Empty() const
	{
		return m_next == this;
	}
}
}

#endif
