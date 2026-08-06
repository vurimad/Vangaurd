/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE PackedArray< BitSize, TElement, TStorage >::PackedArray( const red::memory::Pool& pool )
	: m_data( pool )
	, m_size( 0 )
{
}

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE PackedArray< BitSize, TElement, TStorage >::PackedArray( const PackedArray& other )
	: m_data( other.m_data )
	, m_size( other.m_size )
{
}

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE PackedArray< BitSize, TElement, TStorage >::PackedArray( PackedArray&& other )
	: m_data( std::forward< StorageData >( other.m_data ) )
	, m_size( other.m_size )
{
	other.m_size = 0;
}

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE PackedArray< BitSize, TElement, TStorage >::PackedArray( Uint32 initialSize, const red::memory::Pool& pool )
	: m_data( pool )
	, m_size( 0 )
{
	Resize( initialSize );
}

//////////////////////////////////////////////////////////////////////////

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE PackedArray< BitSize, TElement, TStorage >& PackedArray< BitSize, TElement, TStorage >::operator=( const PackedArray& other )
{
	if ( &other != this )
	{
		m_data = other.m_data;
		m_size = other.m_size;
	}
	return *this;
}

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE PackedArray< BitSize, TElement, TStorage >& PackedArray< BitSize, TElement, TStorage >::operator=( PackedArray&& other )
{
	PackedArray( std::move( other ) ).Swap( *this );
	return *this;
}

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE void PackedArray< BitSize, TElement, TStorage >::Swap( PackedArray& other )
{
	using std::swap;
	m_data.Swap( other.m_data );
	swap( m_size, other.m_size );
}

//////////////////////////////////////////////////////////////////////////

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE Bool PackedArray< BitSize, TElement, TStorage >::operator==( const PackedArray& other ) const
{
	const Uint32 mySize = m_data.Size();
	const Uint32 otherSize = other.m_data.Size();
	if ( mySize > otherSize )
	{
		// first "otherSize" bytes have to be equal
		if ( red::Memcmp( m_data.Data(), other.m_data.Data(), otherSize * sizeof( TStorage ) ) != 0 )
		{
			return false;
		}
		// the rest needs to be zero
		const TStorage* data = m_data.TypedData();
		for ( Uint32 i = otherSize; i < mySize; i++ )
		{
			if ( *( data + i ) != 0 )
			{
				return false;
			}
		}
		return true;
	}
	else if ( mySize < otherSize )
	{
		// first "mySize" bytes have to be equal
		if ( red::Memcmp( m_data.Data(), other.m_data.Data(), mySize * sizeof( TStorage ) ) != 0 )
		{
			return false;
		}
		// the rest needs to be zero
		const TStorage* data = other.m_data.TypedData();
		for ( Uint32 i = mySize; i < otherSize; i++ )
		{
			if ( *( data + i ) != 0 )
			{
				return false;
			}
		}
		return true;
	}
	else
	{
		return red::Memcmp( m_data.Data(), other.m_data.Data(), mySize * sizeof( TStorage ) ) == 0;
	}
}

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE Bool PackedArray< BitSize, TElement, TStorage >::operator!=( const PackedArray& other ) const
{
	return !( *this == other );
}

//////////////////////////////////////////////////////////////////////////

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE void PackedArray< BitSize, TElement, TStorage >::Set( Uint32 i, TElement val )
{
	RED_FATAL_ASSERT( i < m_size, "Index out of bounds " );
	RED_WARNING( ( static_cast< TStorage >( val ) & VALUE_MASK ) == val, "Value out of range" );

	const Uint32 storageIndex = i >> STORAGE_INDEX_SHIFT;
	const Uint32 valueShift = ( i & VALUE_INDEX_MASK ) * BitSize;
	const TStorage storedVal = ( m_data[ storageIndex ] & ( ~( VALUE_MASK << valueShift ) ) ) | ( ( static_cast< TStorage >( val ) & VALUE_MASK ) << valueShift );
	m_data[ storageIndex ] = storedVal;
}

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE TElement PackedArray< BitSize, TElement, TStorage >::Get( Uint32 i ) const
{
	RED_FATAL_ASSERT( i < m_size, "Index out of bounds " );

	const Uint32 storageIndex = i >> STORAGE_INDEX_SHIFT;
	const Uint32 valueShift = ( i & VALUE_INDEX_MASK ) * BitSize;
	return static_cast< TElement >( ( m_data[ storageIndex ] >> valueShift ) & VALUE_MASK );
}

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE void PackedArray< BitSize, TElement, TStorage >::ResetAll()
{
	red::Memset( m_data.Data(), 0, m_data.DataSize() );
}

//////////////////////////////////////////////////////////////////////////

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE void PackedArray< BitSize, TElement, TStorage >::Clear()
{
	m_data.Resize( 0 );
	m_size = 0;
}


template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE void PackedArray< BitSize, TElement, TStorage >::Resize( Uint32 size )
{
	if ( size != m_size )
	{
		m_size = size;
		const Uint32 valuesPerStorage = STORAGE_BITS / BitSize;
		m_data.Resize( ( m_size + valuesPerStorage - 1 ) / valuesPerStorage );
	}
}

template < Uint32 BitSize, typename TElement, typename TStorage >
RED_INLINE void PackedArray< BitSize, TElement, TStorage >::Shrink()
{
	m_data.Shrink();
}

} // red