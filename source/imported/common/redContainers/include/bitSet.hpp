/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < Uint32 MaxSize, typename TStorage >
RED_INLINE BitSetBase< MaxSize, TStorage >::BitSetBase()
{
	ClearAll();
}

//////////////////////////////////////////////////////////////////////////

template < Uint32 MaxSize, typename TStorage >
RED_INLINE Bool BitSetBase< MaxSize, TStorage >::operator==( const BitSetBase& other ) const
{
	return red::Memcmp( m_bits, other.m_bits, INTERNAL_SIZE * sizeof( TStorage )  ) == 0;
}

template < Uint32 MaxSize, typename TStorage >
RED_INLINE Bool BitSetBase< MaxSize, TStorage >::operator!=( const BitSetBase& other ) const
{
	return red::Memcmp( m_bits, other.m_bits, INTERNAL_SIZE * sizeof( TStorage ) ) != 0;
}

template < Uint32 MaxSize, typename TStorage >
RED_INLINE Bool red::BitSetBase<MaxSize, TStorage>::operator<( const BitSetBase& other ) const
{
	return red::Memcmp( m_bits, other.m_bits, INTERNAL_SIZE * sizeof( TStorage ) ) < 0;
}

//////////////////////////////////////////////////////////////////////////

template < Uint32 MaxSize, typename TStorage >
RED_INLINE Bool BitSetBase< MaxSize, TStorage >::Get( Uint32 index ) const
{
	RED_FATAL_ASSERT( index < MaxSize, "Index out of bounds" );
	return ( m_bits[ StorageIndex( index ) ] & Bit( index ) ) != 0;
}

template < Uint32 MaxSize, typename TStorage >
RED_INLINE void BitSetBase< MaxSize, TStorage >::Set( Uint32 index )
{
	RED_FATAL_ASSERT( index < MaxSize, "Index out of bounds" );
	m_bits[ StorageIndex( index ) ] |= Bit( index );
}

template < Uint32 MaxSize, typename TStorage >
RED_INLINE void BitSetBase< MaxSize, TStorage >::Clear( Uint32 index )
{
	RED_FATAL_ASSERT( index < MaxSize, "Index out of bounds" );
	m_bits[ StorageIndex( index ) ] &= ( ~Bit( index ) );
}

template < Uint32 MaxSize, typename TStorage >
RED_INLINE void BitSetBase< MaxSize, TStorage >::Set( Uint32 index, Bool value )
{
	if( value ) Set( index ); else Clear( index );
}

template < Uint32 MaxSize, typename TStorage >
RED_INLINE void BitSetBase< MaxSize, TStorage >::Toggle( Uint32 index )
{
	RED_FATAL_ASSERT( index < MaxSize, "Index out of bounds" );
	m_bits[ StorageIndex( index ) ] ^= Bit( index );
}

template < Uint32 MaxSize, typename TStorage >
RED_INLINE void BitSetBase< MaxSize, TStorage >::SetAll()
{
	red::Memset( m_bits, -1, INTERNAL_SIZE * sizeof( TStorage ) );
	m_bits[ INTERNAL_SIZE - 1 ] = LAST_ENTRY_MASK;
}

template < Uint32 MaxSize, typename TStorage >
RED_INLINE void BitSetBase< MaxSize, TStorage >::ClearAll()
{
	red::Memset( m_bits, 0, INTERNAL_SIZE * sizeof( TStorage ) );
}

template < Uint32 MaxSize, typename TStorage >
RED_INLINE void BitSetBase< MaxSize, TStorage >::ToggleAll()
{
	const TStorage val = TStorage( -1 );
	for ( Uint32 i = 0; i < INTERNAL_SIZE - 1; ++i )
	{
		m_bits[ i ] ^= val;
	}
	m_bits[ INTERNAL_SIZE - 1 ] ^= LAST_ENTRY_MASK;
}

template < Uint32 MaxSize, typename TStorage >
RED_INLINE void BitSetBase< MaxSize, TStorage >::SetBits( Uint32 index, TStorage value )
{
	if( index < INTERNAL_SIZE )
	{
		m_bits[ index ] = value;
	}
}

template < Uint32 MaxSize, typename TStorage >
String BitSetBase< MaxSize, TStorage >::AsString() const
{
	String maskString;
	maskString.Reserve( MaxSize );
	for (Uint32 i = 0; i < MaxSize; i++)
	{
		if (Get( i ))
		{
			maskString.Append( 'T' );
		}
		else
		{
			maskString.Append( 'F' );
		}
	}
	return maskString;
}

template < Uint32 MaxSize, typename TStorage >
void BitSetBase< MaxSize, TStorage >::BuildFromString( const String &inString )
{
	for (Int32 i = 0; i < MaxSize; i++)
	{
		if (inString[i] == 'T')
		{
			Set( i );
		}
		else
		{
			Clear( i );
		}
	}
}

} // red