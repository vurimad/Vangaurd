/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

template < typename TIterator >
struct ContainerOpResult
{
	typedef	TIterator	IteratorType;

	RED_INLINE ContainerOpResult();
	RED_INLINE ContainerOpResult( const ContainerOpResult& other );
	template < typename TOtherIteratorType >
	RED_INLINE ContainerOpResult( const ContainerOpResult< TOtherIteratorType >& other );
	RED_INLINE ContainerOpResult( ContainerOpResult&& other );

	RED_INLINE ContainerOpResult& operator=( const ContainerOpResult& other );
	template < typename TOtherIteratorType >
	RED_INLINE ContainerOpResult& operator=( const ContainerOpResult< TOtherIteratorType >& other );
	RED_INLINE ContainerOpResult& operator=( ContainerOpResult&& other );
	RED_INLINE void Swap( ContainerOpResult& other );

	RED_INLINE Bool			IsSuccessful()	const { return m_success; }
	RED_INLINE IteratorType	Iterator()		const { return m_iterator; }

	static RED_INLINE ContainerOpResult Success( const IteratorType& it ) { return ContainerOpResult( it, true ); }
	static RED_INLINE ContainerOpResult Failure( const IteratorType& it ) { return ContainerOpResult( it, false ); }
	static RED_INLINE ContainerOpResult Failure() { return ContainerOpResult( TIterator(), false ); }

private:

	RED_INLINE ContainerOpResult( const IteratorType& it, Bool success );

	IteratorType	m_iterator;
	Bool			m_success;
};

//////////////////////////////////////////////////////////////////////////

template < typename TIterator >
RED_INLINE ContainerOpResult< TIterator >::ContainerOpResult()
	: m_success( false )
{}

template < typename TIterator >
RED_INLINE ContainerOpResult< TIterator >::ContainerOpResult( const TIterator& it, Bool success )
	: m_iterator( it )
	, m_success( success )
{}

template < typename TIterator >
RED_INLINE ContainerOpResult< TIterator >::ContainerOpResult( const ContainerOpResult& other )
	: m_iterator( other.m_iterator )
	, m_success( other.m_success )
{}

template < typename TIterator >
template < typename TOtherIteratorType >
RED_INLINE ContainerOpResult< TIterator >::ContainerOpResult( const ContainerOpResult< TOtherIteratorType >& other )
	: m_iterator( other.Iterator() )
	, m_success( other.IsSuccessful() )
{}

template < typename TIterator >
RED_INLINE ContainerOpResult< TIterator >::ContainerOpResult( ContainerOpResult&& other )
	: m_iterator( std::move( other.m_iterator ) )
	, m_success( other.m_success )
{}

//////////////////////////////////////////////////////////////////////////

template < typename TIterator >
RED_INLINE ContainerOpResult< TIterator >& ContainerOpResult< TIterator >::operator=( const ContainerOpResult& other )
{
	ContainerOpResult( other ).Swap( *this );
	return *this;
}

template < typename TIterator >
template < typename TOtherIteratorType >
RED_INLINE ContainerOpResult< TIterator >& ContainerOpResult< TIterator >::operator=( const ContainerOpResult< TOtherIteratorType >& other )
{
	ContainerOpResult( other ).Swap( *this );
	return *this;
}

template < typename TIterator >
RED_INLINE ContainerOpResult< TIterator >& ContainerOpResult< TIterator >::operator=( ContainerOpResult&& other )
{
	ContainerOpResult( std::move( other ) ).Swap( *this );
	return *this;
}

template < typename TIterator >
RED_INLINE void ContainerOpResult< TIterator >::Swap( ContainerOpResult& other )
{
	using std::swap;
	swap( m_iterator, other.m_iterator );
	swap( m_success, other.m_success );
}

} // red