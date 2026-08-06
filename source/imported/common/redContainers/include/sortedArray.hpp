/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE SortedArray< TElement, TSortPredicate >::SortedArray( const red::memory::Pool& pool )
	: BaseClass( pool )
	, m_flags( 0 )
{}

template < typename TElement, typename TSortPredicate >
RED_INLINE SortedArray< TElement, TSortPredicate >::SortedArray( const SortedArray& other )
	: BaseClass( other )
	, m_flags( other.m_flags )
{}

template < typename TElement, typename TSortPredicate >
RED_INLINE SortedArray< TElement, TSortPredicate >::SortedArray( SortedArray&& other )
	: BaseClass( std::forward< BaseClass >( other ) )
	, m_flags( other.m_flags )
{
	other.m_flags = 0;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE SortedArray< TElement, TSortPredicate >::SortedArray( std::initializer_list< TElement > initializerList, const red::memory::Pool& pool )
	: BaseClass( initializerList, pool )
	, m_flags( Flag_IsDirty )
{}

template < typename TElement, typename TSortPredicate >
RED_INLINE SortedArray< TElement, TSortPredicate >::SortedArray( Uint32 size, const red::memory::Pool& pool )
	: BaseClass( size, pool )
	, m_flags( 0 )
{}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE SortedArray< TElement, TSortPredicate >& SortedArray< TElement, TSortPredicate >::operator=( const SortedArray& other )
{
	if ( this != &other )
	{
		BaseClass::operator=( other );
		m_flags = other.m_flags;
	}
	return *this;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE SortedArray< TElement, TSortPredicate >& SortedArray< TElement, TSortPredicate >::operator=( SortedArray&& other )
{
	SortedArray( std::move( other ) ).Swap( *this );
	return *this;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE SortedArray< TElement, TSortPredicate >& SortedArray< TElement, TSortPredicate >::operator=( std::initializer_list< TElement > initializerList )
{
	SortedArray( initializerList ).Swap( *this );
	return *this;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::Swap( SortedArray& other )
{
	using namespace std;
	BaseClass::Swap( other );
	swap( m_flags, other.m_flags );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE void* SortedArray< TElement, TSortPredicate >::Data()
{
	MakeClean();
	return BaseClass::Data();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE const void* SortedArray< TElement, TSortPredicate >::Data() const
{
	MakeClean();
	return BaseClass::Data();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE TElement* SortedArray< TElement, TSortPredicate >::TypedData()
{
	MakeClean();
	return BaseClass::TypedData();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE const TElement* SortedArray< TElement, TSortPredicate >::TypedData() const
{
	MakeClean();
	return BaseClass::TypedData();
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::iterator SortedArray< TElement, TSortPredicate >::Begin()
{
	MakeClean();
	return BaseClass::Begin();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::iterator SortedArray< TElement, TSortPredicate >::End()
{
	MakeClean();
	return BaseClass::End();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::const_iterator SortedArray< TElement, TSortPredicate >::Begin() const
{
	MakeClean();
	return BaseClass::Begin();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::const_iterator SortedArray< TElement, TSortPredicate >::End() const
{
	MakeClean();
	return BaseClass::End();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::reverse_iterator SortedArray< TElement, TSortPredicate >::RBegin()
{
	MakeClean();
	return BaseClass::RBegin();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::reverse_iterator SortedArray< TElement, TSortPredicate >::REnd()
{
	MakeClean();
	return BaseClass::REnd();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::reverse_const_iterator SortedArray< TElement, TSortPredicate >::RBegin() const
{
	MakeClean();
	return BaseClass::RBegin();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::reverse_const_iterator SortedArray< TElement, TSortPredicate >::REnd() const
{
	MakeClean();
	return BaseClass::REnd();
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE TElement& SortedArray< TElement, TSortPredicate >::operator[]( Uint32 i )
{
	MakeClean();
	return BaseClass::operator[]( i );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE const TElement& SortedArray< TElement, TSortPredicate >::operator[]( Uint32 i ) const
{
	MakeClean();
	return BaseClass::operator[]( i );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE TElement& SortedArray< TElement, TSortPredicate >::Front()
{
	MakeClean();
	return BaseClass::Front();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE const TElement& SortedArray< TElement, TSortPredicate >::Front() const
{
	MakeClean();
	return BaseClass::Front();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE TElement& SortedArray< TElement, TSortPredicate >::Back()
{
	MakeClean();
	return BaseClass::Back();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE const TElement& SortedArray< TElement, TSortPredicate >::Back() const
{
	MakeClean();
	return BaseClass::Back();
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE Bool SortedArray< TElement, TSortPredicate >::operator==( const SortedArray& other ) const
{
	MakeClean();
	other.MakeClean();
	return BaseClass::operator==( other );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Bool SortedArray< TElement, TSortPredicate >::operator!=( const SortedArray& other ) const
{
	MakeClean();
	other.MakeClean();
	return BaseClass::operator!=( other );
}

//////////////////////////////////////////////////////////////////////////
// Sorted versions of insert/remove which keep array clean

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::Insert( const TElement& element )
{	
	MakeClean();
	iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, SortPredicate() );
	return BaseClass::Insert( it, element );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::Insert( TElement&& element )
{	
	MakeClean();
	iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, SortPredicate() );
	return BaseClass::Insert( it, std::move( element ) );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::InsertUnique( const TElement& element )
{
	MakeClean();
	SortPredicate predicate;
	iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it == BaseClass::End() || predicate( element, *it ) )
	{
		return BaseClass::Insert( it, element );
	}
	return Result::Failure( it );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::InsertUnique( TElement&& element )
{
	MakeClean();
	SortPredicate predicate;
	iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it == BaseClass::End() || predicate( element, *it ) )
	{
		return BaseClass::Insert( it, std::move( element ) );
	}
	return Result::Failure( it );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::Remove( const TElement& element )
{
	MakeClean();
	SortPredicate predicate;
	iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return BaseClass::Remove( it );
	}
	return Result::Failure();
}

//////////////////////////////////////////////////////////////////////////
// "Standard" versions of insert/remove which make array dirty

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::PushBack( const TElement& element )
{
	BaseClass::PushBack( element );
	MakeDirty( BaseClass::End() - 1 );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::PushBack( TElement&& element )
{
	BaseClass::PushBack( std::forward< TElement >( element ) );
	MakeDirty( BaseClass::End() - 1 );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::PushBack( const DynArray< TElement >& arr )
{
	BaseClass::PushBack( arr );
	SetIsDirty( true );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::PushBack( const ArraySpan< const TElement >& arr )
{
	BaseClass::PushBack( arr );
	SetIsDirty( true );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE TElement SortedArray< TElement, TSortPredicate >::PopBack()
{
	MakeClean();
	return BaseClass::PopBack();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::Insert( const_iterator it, const TElement& element )
{
	Result res = BaseClass::Insert( it, element );
	MakeDirty( res.Iterator() );
	return res;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::Insert( const_iterator it, TElement&& element )
{
	Result res = BaseClass::Insert( it, std::forward< TElement >( element ) );
	MakeDirty( res.Iterator() );
	return res;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::InsertAt( const Uint32 index, const TElement& element )
{
	Result res = BaseClass::InsertAt( index, element );
	MakeDirty( res.Iterator() );
	return res;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::InsertAt( const Uint32 index, TElement&& element )
{
	Result res = BaseClass::InsertAt( index, std::forward< TElement >( element ) );
	MakeDirty( res.Iterator() );
	return res;
}

template < typename TElement, typename TSortPredicate >
template < typename... Args >
RED_INLINE TElement& SortedArray< TElement, TSortPredicate >::EmplaceBack( Args&&... args )
{	
	TElement& newElement = BaseClass::EmplaceBack( std::forward< Args >( args )... );
	MakeDirty( BaseClass::End() - 1 );
	return newElement;
}

template < typename TElement, typename TSortPredicate >
template < typename... Args >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::Emplace( const_iterator it, Args&&... args )
{	
	Result res = BaseClass::Emplace( it, std::forward< Args >( args )... );
	MakeDirty( res.Iterator() );
	return res;
}

template < typename TElement, typename TSortPredicate >
template < typename... Args >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::EmplaceAt( const Uint32 index, Args&&... args )
{	
	Result res = BaseClass::EmplaceAt( index, std::forward< Args >( args )... );
	MakeDirty( res.Iterator() );
	return res;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::RemoveReorder( const_iterator it )
{
	Result res = BaseClass::RemoveReorder( it );
	MakeDirty( res.Iterator() );
	return res;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::RemoveReorder( const TElement& element )
{
	Result res = BaseClass::RemoveReorder( element );
	if ( res.IsSuccessful() )
	{
		MakeDirty( res.Iterator() );
	}
	return res;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::RemoveAt( const Uint32 index )
{
	Result res = BaseClass::RemoveAt( index );
	MakeDirty( res.Iterator(), true );
	return res;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::RemoveAt( const Uint32 first, const Uint32 last )
{
	Result res = BaseClass::RemoveAt( first, last );
	MakeDirty( res.Iterator(), true );
	return res;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::Result SortedArray< TElement, TSortPredicate >::RemoveAtReorder( const Uint32 index )
{
	Result res = BaseClass::RemoveAtReorder( index );
	MakeDirty( res.Iterator() );
	return res;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE Int32 SortedArray< TElement, TSortPredicate >::GetIndex( const TElement& element ) const
{
	MakeClean();
	SortPredicate predicate;
	const_iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return static_cast< Int32 >( it - BaseClass::Begin() );
	}
	return -1;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Bool SortedArray< TElement, TSortPredicate >::Exist( const TElement& element ) const
{
	MakeClean();
	return std::binary_search( BaseClass::Begin(), BaseClass::End(), element, SortPredicate() );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::iterator SortedArray< TElement, TSortPredicate >::Find( const TElement& element )
{
	MakeClean();
	SortPredicate predicate;
	iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return it;
	}
	return BaseClass::End();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::const_iterator SortedArray< TElement, TSortPredicate >::Find( const TElement& element ) const
{
	MakeClean();
	SortPredicate predicate;
	const_iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return it;
	}
	return BaseClass::End();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE TElement* SortedArray< TElement, TSortPredicate >::FindPtr( const TElement& element )
{
	MakeClean();
	SortPredicate predicate;
	iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return it.operator->();
	}
	return nullptr;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE const TElement* SortedArray< TElement, TSortPredicate >::FindPtr( const TElement& element ) const
{
	MakeClean();
	SortPredicate predicate;
	const_iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return it.operator->();
	}
	return nullptr;
}

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE Int32 SortedArray< TElement, TSortPredicate >::GetIndex( const TComparableType& element ) const
{
	MakeClean();
	SortPredicate predicate;
	const_iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return static_cast< Int32 >( it - BaseClass::Begin() );
	}
	return -1;
}

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE Bool SortedArray< TElement, TSortPredicate >::Exist( const TComparableType& element ) const
{
	MakeClean();
	return std::binary_search( BaseClass::Begin(), BaseClass::End(), element, SortPredicate() );
}

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::iterator SortedArray< TElement, TSortPredicate >::Find( const TComparableType& element )
{
	MakeClean();
	SortPredicate predicate;
	iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return it;
	}
	return BaseClass::End();
}

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE typename SortedArray< TElement, TSortPredicate >::const_iterator SortedArray< TElement, TSortPredicate >::Find( const TComparableType& element ) const
{
	MakeClean();
	SortPredicate predicate;
	const_iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return it;
	}
	return BaseClass::End();
}

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE TElement* SortedArray< TElement, TSortPredicate >::FindPtr( const TComparableType& element )
{
	MakeClean();
	SortPredicate predicate;
	iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return it.operator->();
	}
	return nullptr;
}

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE const TElement* SortedArray< TElement, TSortPredicate >::FindPtr( const TComparableType& element ) const
{
	MakeClean();
	SortPredicate predicate;
	const_iterator it = std::lower_bound( BaseClass::Begin(), BaseClass::End(), element, predicate );
	if ( it != BaseClass::End() && !predicate( element, *it ) )
	{
		return it.operator->();
	}
	return nullptr;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::Clear()
{
	SetIsDirty( false );
	BaseClass::Clear();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::Resize( Uint32 size )
{
	const Uint32 mySize = this->m_size;
	if ( mySize > 0 )
	{
		if ( size < 2 )
		{
			SetIsDirty( false );
		}
		else if ( size > mySize )
		{
			SetIsDirty( true );
		}
	}
	BaseClass::Resize( size );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::Resize( Uint32 size, const TElement& element )
{
	const Uint32 mySize = this->m_size;
	if(mySize > 0)
	{
		if(size < 2)
		{
			SetIsDirty( false );
		}
		else if(size > mySize)
		{
			SetIsDirty( true );
		}
	}
	BaseClass::Resize( size, element );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::Grow( Uint32 amount )
{
	if ( this->m_size > 0 && amount > 0 )
	{
		SetIsDirty( true );
	}
	BaseClass::Grow( amount );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::Sort()
{
	std::sort( BaseClass::Begin(), BaseClass::End(), SortPredicate() );
	SetIsDirty( false );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::StableSort()
{
	std::stable_sort( BaseClass::Begin(), BaseClass::End(), SortPredicate() );
	SetIsDirty( false );	
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Bool SortedArray< TElement, TSortPredicate >::IsSorted() const
{
	return std::is_sorted( BaseClass::Begin(), BaseClass::End(), SortPredicate() );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE bool SortedArray< TElement, TSortPredicate >::TestSorted()
{
	bool isSorted = std::is_sorted( BaseClass::Begin(), BaseClass::End(), SortPredicate() );
	SetIsDirty( !isSorted );
	return isSorted;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::SetIsDirty( Bool isDirty )
{
	if ( isDirty )
	{
		m_flags |= Flag_IsDirty;
	}
	else
	{
		m_flags &= ( ~Flag_IsDirty );
	}
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::MakeDirty( const_iterator it, Bool afterRemove /* = false */ )
{
	if ( this->m_size < 2 )
	{
		// if less than 2 elements it's automatically clean
		SetIsDirty( false );
		return;
	}
	if ( afterRemove || IsDirty() )
	{
		// no need to change state of already dirty array
		// OR an array after "remove" operation (cause it doesn't change the order)
		return;
	}
	const iterator itEnd = BaseClass::End();
	if ( it < itEnd )
	{
		Bool dirty = false;
		SortPredicate pred;
		if ( it > BaseClass::Begin() )
		{
			dirty = !pred( *( it - 1 ), *it );
		}
		if ( !dirty && it < ( itEnd - 1 ) )
		{
			dirty = !pred( *it, *( it + 1 ) );
		}
		if ( dirty )
		{
			SetIsDirty( true );
		}
	}
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::MakeClean() const
{
	if ( IsDirty() )
	{
		const_cast< SortedArray* >( this )->StableSort();
	}
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::MakeUnique()
{
	const auto begin = Begin(), end = End();
	if ( begin != end )
	{
		std::stable_sort( begin, end );
		auto it = std::unique( begin, end );
		Remove( it, end );
	}
	SetIsDirty( false );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void SortedArray< TElement, TSortPredicate >::MakeUnique() const
{
	const_cast< SortedArray* >( this )->MakeUnique();
}
