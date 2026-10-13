/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_UTILITY_INTRUSIVE_LIST_H_
#define _RED_MEMORY_UTILITY_INTRUSIVE_LIST_H_

#ifdef RED_MEMORY_ENABLE_ASSERTS
// The free list is fragile. If an element inside it is overwritten with 0 (nullptr) than we break the connection between head and tail.
// We cannot check this in runtime because during adding new elements from other threads there is totally valid situation that there is no connection between head and tail.
// To protect against this we replace internally nullptr value with special value (^ 0xAAAA3333AA33A3A3).
// There is much less probable that the memory will be overwritten with this value.
#define RED_MEMORY_DETECT_SLAB_FREE_LIST_NULLPTR_STOMP
#endif


namespace red
{
namespace memory
{
	class IntrusiveSingleLinkedList
	{
	public:
#ifdef RED_MEMORY_DETECT_SLAB_FREE_LIST_NULLPTR_STOMP
		static const u64 c_NextMask = 0xAAAA3333AA33A3A3;
#endif

		IntrusiveSingleLinkedList();

		IntrusiveSingleLinkedList * GetNext() const;
		void SetNext( IntrusiveSingleLinkedList * next );

	private:

		IntrusiveSingleLinkedList * m_next;
	};

	class IntrusiveDoubleLinkedList
	{
	public:

		IntrusiveDoubleLinkedList();

		void PushFront( IntrusiveDoubleLinkedList * node );
		void Remove();

		IntrusiveDoubleLinkedList * GetNext() const;
		IntrusiveDoubleLinkedList * GetPrevious() const;

		bool Empty() const;

	private:

		IntrusiveDoubleLinkedList * m_next;
		IntrusiveDoubleLinkedList * m_previous;

	};
}
}

#include "intrusiveList.hpp"

#endif
