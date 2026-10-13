/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer >::CheckedConstIterator()
	: m_container( nullptr )
	, m_ptr( nullptr )
{}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer >::CheckedConstIterator( const TContainer* container, PtrType ptr )
	: m_container( const_cast< TContainer* >( container ) )
	, m_ptr( ptr )
{
	RED_FATAL_ASSERT( IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer >::CheckedConstIterator( const CheckedConstIterator& it )
	: m_container( it.m_container )
	, m_ptr( it.m_ptr )
{
	RED_FATAL_ASSERT( IsValidOrEnd() || IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer >::operator bool() const
{ 
	return IsValid();
}

template < typename TContainer >
RED_INLINE Bool CheckedConstIterator< TContainer >::operator!() const
{
	return !IsValid();
}

template < typename TContainer >
RED_INLINE Bool CheckedConstIterator< TContainer >::IsValid() const
{
	return m_container != nullptr && m_ptr >= m_container->TypedData() && m_ptr < m_container->TypedData() + m_container->Size();
}

template < typename TContainer >
RED_INLINE Bool CheckedConstIterator< TContainer >::IsValidOrEnd() const
{
	// There are some usecases that we iterate from `end()-1` to `begin()-1`
	return m_container != nullptr && m_ptr + 1 >= m_container->TypedData() && m_ptr <= m_container->TypedData() + m_container->Size();
}

template < typename TContainer >
RED_INLINE Bool CheckedConstIterator< TContainer >::IsNull() const
{
	return m_container == nullptr && m_ptr == nullptr;
}

template < typename TContainer >
RED_INLINE typename CheckedConstIterator< TContainer >::RefType CheckedConstIterator< TContainer >::operator*() const
{
	RED_FATAL_ASSERT( IsValid(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	return *this->m_ptr;
}

template < typename TContainer >
RED_INLINE typename CheckedConstIterator< TContainer >::PtrType CheckedConstIterator< TContainer >::operator->() const
{
	RED_FATAL_ASSERT( IsValid(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	return this->m_ptr;
}

template < typename TContainer >
RED_INLINE typename CheckedConstIterator< TContainer >::RefType CheckedConstIterator< TContainer >::operator[]( DiffType count ) const
{
	RED_FATAL_ASSERT( IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	const PtrType realPtr = this->m_ptr + count;
	RED_FATAL_ASSERT( realPtr >= m_container->TypedData() && realPtr < m_container->TypedData() + m_container->Size(), "Invalid offset %ull", count );
	return *realPtr;
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer >& CheckedConstIterator< TContainer >::operator++()
{
	this->m_ptr++;
	RED_FATAL_ASSERT( IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	return *this;
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer > CheckedConstIterator< TContainer >::operator++(int)
{
	return CheckedConstIterator( this->m_container, this->m_ptr++ );
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer >& CheckedConstIterator< TContainer >::operator--()
{
	this->m_ptr--;
	RED_FATAL_ASSERT( IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	return *this;
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer > CheckedConstIterator< TContainer >::operator--(int)
{
	return CheckedConstIterator( this->m_container, this->m_ptr-- );
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer > CheckedConstIterator< TContainer >::operator+( DiffType count ) const
{
	return CheckedConstIterator( this->m_container, this->m_ptr + count );
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer > CheckedConstIterator< TContainer >::operator-( DiffType count ) const
{
	return CheckedConstIterator( this->m_container, this->m_ptr - count );
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer > operator+( typename CheckedConstIterator< TContainer >::DiffType count, const CheckedConstIterator< TContainer >& iter )
{
	return CheckedConstIterator< TContainer >( iter.m_container, iter.m_ptr + count );
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer >& CheckedConstIterator< TContainer >::operator+=( DiffType count )
{
	this->m_ptr += count;
	RED_FATAL_ASSERT( IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	return *this;
}

template < typename TContainer >
RED_INLINE CheckedConstIterator< TContainer >& CheckedConstIterator< TContainer >::operator-=( DiffType count )
{
	this->m_ptr -= count;
	RED_FATAL_ASSERT( IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	return *this;
}

template < typename TContainer >
RED_INLINE typename CheckedConstIterator< TContainer >::DiffType CheckedConstIterator< TContainer >::operator-( const CheckedConstIterator& it ) const
{
	RED_FATAL_ASSERT( this->m_container == it.m_container, "Comparing iterators of two different containers! m_container = 0x%p, it.m_container = 0x%p.", this->m_container, it.m_container );
	RED_FATAL_ASSERT( IsValidOrEnd() || IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	RED_FATAL_ASSERT( it.IsValidOrEnd() || it.IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", it.m_container, it.m_ptr );
	return this->m_ptr - it.m_ptr;
}

template < typename TContainer >
RED_INLINE Bool CheckedConstIterator< TContainer >::operator==( const CheckedConstIterator& it ) const
{
	RED_FATAL_ASSERT( this->m_container == it.m_container, "Comparing iterators of two different containers! m_container = 0x%p, it.m_container = 0x%p.", this->m_container, it.m_container );
	RED_FATAL_ASSERT( IsValidOrEnd() || IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	RED_FATAL_ASSERT( it.IsValidOrEnd() || it.IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", it.m_container, it.m_ptr );
	return this->m_ptr == it.m_ptr;
}

template < typename TContainer >
RED_INLINE Bool CheckedConstIterator< TContainer >::operator!=( const CheckedConstIterator& it ) const
{
	RED_FATAL_ASSERT( this->m_container == it.m_container, "Comparing iterators of two different containers! m_container = 0x%p, it.m_container = 0x%p.", this->m_container, it.m_container );
	RED_FATAL_ASSERT( IsValidOrEnd() || IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	RED_FATAL_ASSERT( it.IsValidOrEnd() || it.IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", it.m_container, it.m_ptr );
	return this->m_ptr != it.m_ptr;
}

template < typename TContainer >
RED_INLINE Bool CheckedConstIterator< TContainer >::operator<( const CheckedConstIterator& it ) const
{
	RED_FATAL_ASSERT( this->m_container == it.m_container, "Comparing iterators of two different containers! m_container = 0x%p, it.m_container = 0x%p.", this->m_container, it.m_container );
	RED_FATAL_ASSERT( IsValidOrEnd() || IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	RED_FATAL_ASSERT( it.IsValidOrEnd() || it.IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", it.m_container, it.m_ptr );
	return this->m_ptr < it.m_ptr;
}

template < typename TContainer >
RED_INLINE Bool CheckedConstIterator< TContainer >::operator<=( const CheckedConstIterator& it ) const
{
	RED_FATAL_ASSERT( this->m_container == it.m_container, "Comparing iterators of two different containers! m_container = 0x%p, it.m_container = 0x%p.", this->m_container, it.m_container );
	RED_FATAL_ASSERT( IsValidOrEnd() || IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	RED_FATAL_ASSERT( it.IsValidOrEnd() || it.IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", it.m_container, it.m_ptr );
	return this->m_ptr <= it.m_ptr;
}

template < typename TContainer >
RED_INLINE Bool CheckedConstIterator< TContainer >::operator>( const CheckedConstIterator& it ) const
{
	RED_FATAL_ASSERT( this->m_container == it.m_container, "Comparing iterators of two different containers! m_container = 0x%p, it.m_container = 0x%p.", this->m_container, it.m_container );
	RED_FATAL_ASSERT( IsValidOrEnd() || IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	RED_FATAL_ASSERT( it.IsValidOrEnd() || it.IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", it.m_container, it.m_ptr );
	return this->m_ptr > it.m_ptr;
}

template < typename TContainer >
RED_INLINE Bool CheckedConstIterator< TContainer >::operator>=( const CheckedConstIterator& it ) const
{
	RED_FATAL_ASSERT( this->m_container == it.m_container, "Comparing iterators of two different containers! m_container = 0x%p, it.m_container = 0x%p.", this->m_container, it.m_container );
	RED_FATAL_ASSERT( IsValidOrEnd() || IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", m_container, m_ptr );
	RED_FATAL_ASSERT( it.IsValidOrEnd() || it.IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", it.m_container, it.m_ptr );
	return this->m_ptr >= it.m_ptr;
}

//////////////////////////////////////////////////////////////////////////

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer >::CheckedIterator()
{}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer >::CheckedIterator( TContainer* container, const PtrType ptr )
	: BaseClass( container, ptr )
{}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer >::CheckedIterator( const CheckedIterator& it )
	: BaseClass( it )
{}

template < typename TContainer >
RED_INLINE typename CheckedIterator< TContainer >::RefType CheckedIterator< TContainer >::operator*() const
{
	RED_FATAL_ASSERT( this->IsValid(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", this->m_container, this->m_ptr );
	return const_cast< RefType >( *this->m_ptr );
}

template < typename TContainer >
RED_INLINE typename CheckedIterator< TContainer >::PtrType CheckedIterator< TContainer >::operator->() const
{
	RED_FATAL_ASSERT( this->IsValid(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", this->m_container, this->m_ptr );
	return const_cast< PtrType >( this->m_ptr );
}

template < typename TContainer >
RED_INLINE typename CheckedIterator< TContainer >::RefType CheckedIterator< TContainer >::operator[]( DiffType count ) const
{
	RED_FATAL_ASSERT( this->IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", this->m_container, this->m_ptr );
	const PtrType realPtr = const_cast<const PtrType>(this->m_ptr + count);
	RED_FATAL_ASSERT( realPtr >= this->m_container->TypedData() && realPtr < this->m_container->TypedData() + this->m_container->Size(), "Invalid offset %ull", count );
	return const_cast< RefType >( *realPtr );
}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer >& CheckedIterator< TContainer >::operator++()
{
	this->m_ptr++;
	RED_FATAL_ASSERT( this->IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", this->m_container, this->m_ptr );
	return *this;
}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer > CheckedIterator< TContainer >::operator++(int)
{
	return CheckedIterator( this->m_container, const_cast< PtrType >( this->m_ptr++ ) );
}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer >& CheckedIterator< TContainer >::operator--()
{
	this->m_ptr--;
	RED_FATAL_ASSERT( this->IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", this->m_container, this->m_ptr );
	return *this;
}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer > CheckedIterator< TContainer >::operator--(int)
{
	return CheckedIterator( this->m_container, const_cast< PtrType >( this->m_ptr-- ) );
}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer > CheckedIterator< TContainer >::operator+( DiffType count ) const
{
	return CheckedIterator( this->m_container, const_cast< PtrType >( this->m_ptr + count ) );
}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer > CheckedIterator< TContainer >::operator-( DiffType count ) const
{
	return CheckedIterator( this->m_container, const_cast< PtrType >( this->m_ptr - count ) );
}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer > operator+( typename CheckedIterator< TContainer >::DiffType count, const CheckedIterator< TContainer >& iter )
{
	return CheckedIterator< TContainer >( iter.m_container, const_cast< typename CheckedIterator< TContainer >::PtrType >( iter.m_ptr + count ) );
}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer >& CheckedIterator< TContainer >::operator+=( DiffType count )
{
	this->m_ptr += count;
	RED_FATAL_ASSERT( this->IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", this->m_container, this->m_ptr );
	return *this;
}

template < typename TContainer >
RED_INLINE CheckedIterator< TContainer >& CheckedIterator< TContainer >::operator-=( DiffType count )
{
	this->m_ptr -= count;
	RED_FATAL_ASSERT( this->IsValidOrEnd(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", this->m_container, this->m_ptr );
	return *this;
}

template < typename TContainer >
RED_INLINE typename CheckedIterator< TContainer >::DiffType CheckedIterator< TContainer >::operator-( const CheckedIterator& it ) const
{
	RED_FATAL_ASSERT( this->m_container == it.m_container, "Comparing iterators of two different containers! m_container = 0x%p, it.m_container = 0x%p.", this->m_container, it.m_container );
	RED_FATAL_ASSERT( this->IsValidOrEnd() || this->IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", this->m_container, this->m_ptr );
	RED_FATAL_ASSERT( it.IsValidOrEnd() || it.IsNull(), "Invalid iterator! m_container = 0x%p, m_ptr = 0x%p.", it.m_container, it.m_ptr );
	return this->m_ptr - it.m_ptr;
}

template < typename TContainer >
RED_INLINE typename CheckedIterator< TContainer >::DiffType CheckedIterator< TContainer >::operator-( const CheckedConstIterator< TContainer >& it ) const
{
	return BaseClass::operator-( it );
}

//////////////////////////////////////////////////////////////////////////

template < typename TPtrDiff, typename TContainer >
RED_INLINE TPtrDiff Distance( CheckedConstIterator< TContainer > begin, CheckedConstIterator< TContainer > end )
{
	const typename CheckedConstIterator< TContainer >::DiffType diff = end - begin;
	RED_SYSTEM_ASSERT( diff >= std::numeric_limits< TPtrDiff >::lowest() && diff <= std::numeric_limits< TPtrDiff >::max(), "Distance cannot store value in specified type." );
	return static_cast< TPtrDiff >( diff );
}

} // red
