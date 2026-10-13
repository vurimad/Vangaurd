/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

RED_INLINE IntrusiveListNode::IntrusiveListNode()
{
	Unlink();
}

RED_INLINE IntrusiveListNode::IntrusiveListNode( IntrusiveListNode&& other )
{
	if ( other.Unlinked() )
	{
		Unlink();
	}
	else
	{
		m_prev = other.m_prev;
		m_next = other.m_next;
		other.Unlink();
		UpdateNeighbours();
	}
}

RED_INLINE IntrusiveListNode::~IntrusiveListNode()
{
	Remove();
}

RED_INLINE IntrusiveListNode& IntrusiveListNode::operator=( IntrusiveListNode&& other )
{
	IntrusiveListNode( std::move( other ) ).Swap( *this );
	return *this;
}

RED_INLINE void IntrusiveListNode::Swap( IntrusiveListNode& other )
{
	using namespace std;

	const Bool thisUnlinked = Unlinked();
	const Bool otherUnlinked = other.Unlinked();
	swap( m_prev, other.m_prev );
	swap( m_next, other.m_next );
	if ( thisUnlinked )
	{
		other.Unlink();
	}
	else
	{
		other.UpdateNeighbours();
	}
	if ( otherUnlinked )
	{
		Unlink();
	}
	else
	{
		UpdateNeighbours();
	}
}

RED_INLINE void IntrusiveListNode::Insert( IntrusiveListNode& node )
{
	node.Remove();
	m_next->m_prev = &node;
	node.m_next = m_next;
	node.m_prev = this;
	m_next = &node;

	DebugValidate();
}

RED_INLINE void IntrusiveListNode::Remove()
{
	m_prev->m_next = m_next;
	m_next->m_prev = m_prev;
	Unlink();
}

RED_INLINE void IntrusiveListNode::UpdateNeighbours()
{
	m_prev->m_next = this;
	m_next->m_prev = this;
}

RED_INLINE void IntrusiveListNode::Unlink()
{
	m_prev = this;
	m_next = this;
}

RED_INLINE void IntrusiveListNode::DebugValidate()
{
#ifndef RED_CONFIGURATION_FINAL
	RED_FATAL_ASSERT( m_prev->m_next == this, "IntrusiveList corrupted" );
	RED_FATAL_ASSERT( m_next->m_prev == this, "IntrusiveList corrupted" );
#endif
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE IntrusiveList< TElement >::IntrusiveList()
{}

template < typename TElement >
RED_INLINE IntrusiveList< TElement >::IntrusiveList( IntrusiveList&& other )
	: IntrusiveListNode( std::forward< IntrusiveListNode >( other ) )
{
}

template < typename TElement >
RED_INLINE IntrusiveList< TElement >::~IntrusiveList()
{
}

template < typename TElement >
RED_INLINE IntrusiveList< TElement >& IntrusiveList< TElement >::operator=( IntrusiveList&& other )
{
	IntrusiveListNode::operator=( std::forward< IntrusiveListNode >( other ) );
	return *this;
}

template < typename TElement >
RED_INLINE void IntrusiveList< TElement >::PushFront( TElement& element )
{
	IntrusiveListNode::Insert( element );
}

template < typename TElement >
RED_INLINE void IntrusiveList< TElement >::PushBack( TElement& element )
{
	m_prev->Insert( element );
}

template < typename TElement >
RED_INLINE typename IntrusiveList< TElement >::iterator IntrusiveList< TElement >::Insert( iterator it, TElement& element )
{
	IntrusiveListNode* myNode = it.m_node;
	if ( myNode == nullptr )
	{
		// if iterator points to nullptr we need to find the last node to do "push back"
		myNode = m_prev;
	}
	myNode->Insert( element );
	return iterator( this, myNode->m_next );
}

template < typename TElement >
RED_INLINE typename IntrusiveList< TElement >::iterator IntrusiveList< TElement >::Remove( iterator it )
{
	IntrusiveListNode* node = it.m_node;
	if ( node != nullptr )
	{
		IntrusiveListNode* next = node->m_next;
		node->Remove();
		if ( next == this )
		{
			return End();
		}
		return iterator( this, next );
	}
	return it;
}

template < typename TElement >
RED_INLINE void IntrusiveList< TElement >::Join( IntrusiveList&& other )
{
	if ( !other.Empty() )
	{
		// link end of other list with beginning of the current one
		m_next->m_prev = other.m_prev;
		m_next->m_prev->m_next = m_next;

		// link beginning of the other list with "head"
		m_next = other.m_next;
		m_next->m_prev = this;

		other.Unlink();
	}

	DebugValidate();
}

template < typename TElement >
RED_INLINE void IntrusiveList< TElement >::Clear()
{
	IntrusiveListNode* node = this;
	do
	{
		IntrusiveListNode* next = node->m_next;
		node->Unlink();
		node = next;
	} while ( node != this );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE IntrusiveList< TElement >::const_iterator::const_iterator( const IntrusiveList* base, const IntrusiveListNode* node )
	: m_base( const_cast< IntrusiveList* >( base ) )
	, m_node( const_cast< IntrusiveListNode* >( node ) )
{
}

template < typename TElement >
RED_INLINE typename IntrusiveList< TElement >::const_iterator& IntrusiveList< TElement >::const_iterator::operator++()
{
	// if we didn't reach the end yet
	if ( m_node != nullptr )
	{
		m_node = m_node->m_next;
		// did we reach the end?
		if ( m_node == m_base )
		{
			m_node = nullptr;
		}
	}
	return *this;
}

template < typename TElement >
RED_INLINE typename IntrusiveList< TElement >::const_iterator& IntrusiveList< TElement >::const_iterator::operator--()
{
	// are we at the end?
	if ( m_node == nullptr )
	{
		m_node = m_base->m_prev;
	}
	// if we didn't reach the beginning yet
	else if ( m_node->m_prev != m_base )
	{
		m_node = m_node->m_prev;
	}
	return *this;
}

template < typename TElement >
RED_INLINE Bool IntrusiveList< TElement >::const_iterator::operator==( const const_iterator& other ) const
{
	RED_ASSERT( m_base == other.m_base, "Comparing iterators of two different lists" );
	return m_node == other.m_node;
}

template < typename TElement >
RED_INLINE Bool IntrusiveList< TElement >::const_iterator::operator!=( const const_iterator& other ) const
{
	RED_ASSERT( m_base == other.m_base, "Comparing iterators of two different lists" );
	return m_node != other.m_node;
}

} // red