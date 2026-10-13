/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TStorage >
RED_INLINE BitSetDynamicBase< TStorage >::BitSetDynamicBase( const red::memory::Pool& pool )
	: m_bits( pool )
	, m_bitSize( 0 )
{
}

template < typename TStorage >
RED_INLINE BitSetDynamicBase< TStorage >::BitSetDynamicBase( const BitSetDynamicBase& other )
	: m_bits( other.m_bits )
	, m_bitSize( other.m_bitSize )
{
}

template < typename TStorage >
RED_INLINE BitSetDynamicBase< TStorage >::BitSetDynamicBase( BitSetDynamicBase&& other )
	: m_bits( std::forward< StorageData >( other.m_bits ) )
	, m_bitSize( other.m_bitSize )
{
	other.m_bitSize = 0;
}

template < typename TStorage >
RED_INLINE BitSetDynamicBase< TStorage >::BitSetDynamicBase( Uint32 initialSize, const red::memory::Pool& pool )
	: m_bits( pool )
	, m_bitSize( 0 )
{
	Resize( initialSize );
}

template < typename TStorage >
RED_INLINE BitSetDynamicBase< TStorage >::~BitSetDynamicBase()
{
}

//////////////////////////////////////////////////////////////////////////

template < typename TStorage >
RED_INLINE BitSetDynamicBase< TStorage >& BitSetDynamicBase< TStorage >::operator=( const BitSetDynamicBase& other )
{
	if ( &other != this )
	{
		m_bits = other.m_bits;
		m_bitSize = other.m_bitSize;
	}
	return *this;
}

template < typename TStorage >
RED_INLINE BitSetDynamicBase< TStorage >& BitSetDynamicBase< TStorage >::operator=( BitSetDynamicBase&& other )
{
	BitSetDynamicBase( std::move( other ) ).Swap( *this );
	return *this;
}

template < typename TStorage >
RED_INLINE void BitSetDynamicBase< TStorage >::Swap( BitSetDynamicBase& other )
{
	using namespace std;
	m_bits.Swap( other.m_bits );
	swap( m_bitSize, other.m_bitSize );
}

//////////////////////////////////////////////////////////////////////////

template < typename TStorage >
RED_INLINE Bool BitSetDynamicBase< TStorage >::operator==( const BitSetDynamicBase& other ) const
{
	const Uint32 mySize = m_bits.Size();
	const Uint32 otherSize = other.m_bits.Size();
	if ( mySize > otherSize )
	{
		// first "otherSize" bytes have to be equal
		if ( red::Memcmp( m_bits.Data(), other.m_bits.Data(), otherSize * sizeof( TStorage ) ) != 0 )
		{
			return false;
		}
		// the rest needs to be zero
		const TStorage* data = m_bits.TypedData();
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
		if ( red::Memcmp( m_bits.Data(), other.m_bits.Data(), mySize * sizeof( TStorage ) ) != 0 )
		{
			return false;
		}
		// the rest needs to be zero
		const TStorage* data = other.m_bits.TypedData();
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
		return red::Memcmp( m_bits.Data(), other.m_bits.Data(), mySize * sizeof( TStorage ) ) == 0;
	}
}

template < typename TStorage >
RED_INLINE Bool BitSetDynamicBase< TStorage >::operator!=( const BitSetDynamicBase& other ) const
{
	return !( *this == other );
}

//////////////////////////////////////////////////////////////////////////

template < typename TStorage >
RED_INLINE Bool BitSetDynamicBase< TStorage >::Get( Uint32 index ) const
{
	return ( m_bits[ StorageIndex( index ) ] & Bit( index ) ) != 0;
}

template < typename TStorage >
RED_INLINE void BitSetDynamicBase< TStorage >::Set( Uint32 index )
{
	m_bits[ StorageIndex( index ) ] |= Bit( index );
}

template < typename TStorage >
RED_INLINE void BitSetDynamicBase< TStorage >::Clear( Uint32 index )
{
	m_bits[ StorageIndex( index ) ] &= ( ~Bit( index ) );
}

template < typename TStorage >
RED_INLINE void BitSetDynamicBase< TStorage >::Toggle( Uint32 index )
{
	m_bits[ StorageIndex( index ) ] ^= Bit( index );
}

template < typename TStorage >
RED_INLINE void BitSetDynamicBase< TStorage >::SetAll()
{
	const Uint32 size = m_bits.Size();
	if ( size > 0 )
	{
		red::Memset( m_bits.Data(), -1, size * sizeof( TStorage ) );
		m_bits[ size - 1 ] = LastEntryMask();
	}
}

template < typename TStorage >
RED_INLINE void BitSetDynamicBase< TStorage >::ClearAll()
{
	red::Memset( m_bits.Data(), 0, m_bits.Size() * sizeof( TStorage ) );
}

template < typename TStorage >
RED_INLINE void BitSetDynamicBase< TStorage >::ToggleAll()
{
	const Uint32 size = m_bits.Size();
	if ( size > 0 )
	{
		const TStorage val = TStorage( -1 );
		for ( Uint32 i = 0; i < size - 1; ++i )
		{
			m_bits[ i ] ^= val;
		}
		m_bits[ size - 1 ] ^= LastEntryMask();
	}
}

//////////////////////////////////////////////////////////////////////////

template < typename TStorage >
RED_INLINE void BitSetDynamicBase< TStorage >::Resize( Uint32 size )
{
	if ( size != m_bitSize )
	{
		m_bitSize = size;
		m_bits.Resize( ( m_bitSize + STORAGE_BITS - 1 ) / STORAGE_BITS );
	}
}

template < typename TStorage >
RED_INLINE void red::BitSetDynamicBase<TStorage>::RemoveAtReorder( Uint32 index )
{
	RED_FATAL_ASSERT( index < m_bitSize, "index is out of bounds" );

	const Uint32 last = m_bitSize - 1;
	if ( index != last )
	{
		if ( Get( last ) )
			Set( index );
		else
			Clear( index );
	}

	Resize( last );
}

template < typename TStorage >
RED_INLINE void BitSetDynamicBase< TStorage >::Shrink()
{
	m_bits.Shrink();
}

} // red