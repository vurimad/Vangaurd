/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////
// Intrusive (circular) list.
// Supports forward and backward iteration.
// Does not call constructors/destructors.
//////////////////////////////////////////////////////////////////////////

template < typename TElement > class IntrusiveList;

/////////////////////////////////////////////////////////////////////////

class IntrusiveListNode : public red::NonCopyable
{
public:

	// Default constructor
	RED_INLINE IntrusiveListNode();
	// Move constructor
	RED_INLINE IntrusiveListNode( IntrusiveListNode&& other );
	// Destructor
	RED_INLINE ~IntrusiveListNode();
	// Move assignment
	RED_INLINE IntrusiveListNode& operator=( IntrusiveListNode&& other );

	// Insert 'node' to the list after the current one
	RED_INLINE void Insert( IntrusiveListNode& node );
	// Removes node from the list
	RED_INLINE void Remove();

protected:

	IntrusiveListNode*	m_prev;
	IntrusiveListNode*	m_next;

private:

	RED_INLINE void Swap( IntrusiveListNode& other );
	RED_INLINE void UpdateNeighbours();
	RED_INLINE void Unlink();
	RED_INLINE Bool Unlinked() const { return m_next == this; }
	RED_INLINE void DebugValidate();

	template < typename U >
	friend class IntrusiveList;
};

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
class IntrusiveList : public IntrusiveListNode
{
public:

	class const_iterator
	{
	public:

		// STL compatibility
		typedef std::bidirectional_iterator_tag		iterator_category;
		typedef TElement							value_type;
		typedef Int64								difference_type;
		typedef const TElement*						pointer;
		typedef const TElement&						reference;

		RED_INLINE const_iterator& operator++();
		RED_INLINE const_iterator& operator--();
		RED_INLINE const TElement* operator->() const { return static_cast< const TElement* >( m_node ); }
		RED_INLINE const TElement& operator*() const { return *static_cast< const TElement* >( m_node ); }
		RED_INLINE Bool operator==( const const_iterator& other ) const;
		RED_INLINE Bool operator!=( const const_iterator& other ) const;

	protected:

		RED_INLINE const_iterator( const IntrusiveList* base, const IntrusiveListNode* node );

		IntrusiveList*			m_base;
		IntrusiveListNode*		m_node;

		friend class IntrusiveList;
	};


	class iterator : public const_iterator
	{
	public:

		// STL compatibility
		typedef TElement*							pointer;
		typedef TElement&							reference;

		RED_INLINE iterator& operator++() { const_iterator::operator++(); return *this; }
		RED_INLINE iterator& operator--() { const_iterator::operator--(); return *this; }
		RED_INLINE TElement* operator->() const { return static_cast< TElement* >( this->m_node ); }
		RED_INLINE TElement& operator*() const { return *static_cast< TElement* >( this->m_node ); }

	private:

		RED_INLINE iterator( const IntrusiveList* base, const IntrusiveListNode* node ) : const_iterator( base, node ) {}

		friend class IntrusiveList;
	};

	typedef TElement								ElementType;
	typedef std::reverse_iterator< iterator >		reverse_iterator;
	typedef std::reverse_iterator< const_iterator >	reverse_const_iterator;

	// Default constructor
	RED_INLINE IntrusiveList();
	// Move constructor
	RED_INLINE IntrusiveList( IntrusiveList&& other );
	// Destructor
	RED_INLINE ~IntrusiveList();
	// Move assignment
	RED_INLINE IntrusiveList& operator=( IntrusiveList&& other );

	// Returns true if list contains 0 elements
	RED_INLINE Bool Empty() const { return Unlinked(); }
	// Get iterator pointing to the first element on the list
	RED_INLINE iterator Begin()	{ return Empty() ? End() : iterator( this, m_next ); }
	// Get iterator pointing to the element next after the last one
	RED_INLINE iterator End() { return iterator( this, nullptr ); }
	// Get const_iterator pointing to the first element on the list
	RED_INLINE const_iterator Begin() const { return Empty() ? End() : const_iterator( this, m_next ); }
	// Get const_iterator pointing to the element next after the last one
	RED_INLINE const_iterator End() const{ return const_iterator( this, nullptr ); }
	// Get reverse_iterator pointing to the last element on the list
	RED_INLINE reverse_iterator RBegin() { return reverse_iterator( End() ); }
	// Get reverse_iterator pointing to the element before the first one
	RED_INLINE reverse_iterator REnd() { return reverse_iterator( Begin() ); }
	// Get reverse_const_iterator pointing to the last element on the list
	RED_INLINE reverse_const_iterator RBegin() const { return reverse_const_iterator( End() ); }
	// Get reverse_const_iterator pointing to the element before the first one
	RED_INLINE reverse_const_iterator REnd() const{ return reverse_const_iterator( Begin() ); }
	// Get pointer to the first element; returns nullptr if the list is empty
	RED_INLINE TElement* Front() const { return Empty() ? nullptr : static_cast< TElement* >( m_next ); }
	// Get pointer to the last element; returns nullptr if the list is empty
	RED_INLINE TElement* Back() const { return Empty() ? nullptr : static_cast< TElement* >( m_prev ); }

	// Add 'element' at the beginning of the list
	RED_INLINE void PushFront( TElement& element );
	// Add 'element' at the end of the list
	RED_INLINE void PushBack( TElement& element );
	// Add 'element' after the one pointed by the iterator 'it'; returns iterator pointing to the inserted element
	RED_INLINE iterator Insert( iterator it, TElement& element );
	// Remove element pointed by the iterator 'it'; returns iterator pointing to the element next after the removed one
	RED_INLINE iterator Remove( iterator it );
	// Move all elements from 'other' at the beginning of current list
	RED_INLINE void Join( IntrusiveList&& other );
	// Remove all elements from the list
	RED_INLINE void Clear();
};

} // red

#include "intrusiveList.hpp"
