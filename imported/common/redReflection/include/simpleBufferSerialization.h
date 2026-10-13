/*
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redContainers/include/sortedArray.h"

class CSimpleBufferWriter : public red::NonCopyable
{
public:
	CSimpleBufferWriter( red::DynArray< Int8 >& data, Uint16 fileVersion )
		: m_data( data )													{ Put( fileVersion ); }

	template< class T >
	RED_INLINE void Put( const T& val )
	{
		Write( &val, sizeof( T ) );
	}
	template< class T >
	RED_INLINE void Pop( const T& val )
	{
		m_data.Resize( m_data.Size() - sizeof( T ) );
	}
	template< class T >
	RED_INLINE void SmartPut( const T& val )								{ RED_HALT( "Type not implemented" ); }	
	template< class T >
	RED_INLINE void SmartPut( const red::DynArray< T >& val )
	{
		RED_ASSERT( val.Size() <= 0xffff );
		Uint16 count = Uint16( val.Size() );
		Put( count );
		if ( count )
		{
			Write( val.TypedData(), static_cast< Uint32 >( val.DataSize() ) );
		}
	}
	template< class T >
	RED_INLINE void SmartPutLarge( const red::DynArray< T >& val )
	{
		RED_ASSERT( val.Size() <= 0xffffffff );
		Uint32 count = Uint32( val.Size() );
		Put( count );
		if ( count )
		{
			Write( val.TypedData(), static_cast< Uint32 >( val.DataSize() ) );
		}
	}
	template < class T, class SortPredicate >
	RED_INLINE void SmartPut( const red::SortedArray< T, SortPredicate >& val )
	{
		SmartPut( static_cast< const red::DynArray< T >& >( val ) );
	}
	template < class T, class SortPredicate >
	RED_INLINE void SmartPutLarge( const red::SortedArray< T, SortPredicate >& val )
	{
		SmartPutLarge( static_cast< const red::DynArray< T >& >( val ) );
	}

	RED_INLINE void Write( const void* dataPtr, Uint32 dataSize )
	{
		Uint32 prevSize = m_data.Size();
		Uint32 newSize = prevSize + dataSize;
		if ( newSize > m_data.Capacity() )
		{
			m_data.Reserve( Max( 128U, newSize*2 ) );
		}
		m_data.Resize( newSize );
		red::Memcpy( &m_data[ prevSize ], dataPtr, dataSize );
	}

	template < class T >
	RED_INLINE Uint32 ReserveSpace( const T& val )
	{
		Uint32 place = m_data.Size();
		Put( val );
		return place;
	}
	template < class T >
	RED_INLINE void PutReserved( const T& val, Uint32 place )
	{
		red::Memcpy( &m_data[ place ], &val, sizeof( T ) );
	}
	RED_INLINE Uint32 GetCurrentPosition()
	{
		return m_data.Size();
	}
protected:
	red::DynArray< Int8 >&				m_data;
};

class CSimpleBufferReader : public red::NonCopyable
{
public:
	CSimpleBufferReader( const red::DynArray< Int8 >& data )
		: m_data( data.TypedData() )
		, m_dataSize( data.Size() )
		, m_currentPos( 0 )												{ if ( !Get( m_fileVersion ) ) m_fileVersion = 0xffff; }

	CSimpleBufferReader( const Int8* data, Uint32 dataSize )
		: m_data( data )
		, m_dataSize( dataSize )
		, m_currentPos( 0 )												{ if ( !Get( m_fileVersion ) ) m_fileVersion = 0xffff; }

	template< class T >
	RED_INLINE Bool Get( T& val )
	{
		return Read( &val, sizeof( T ) );
	}
	template< class T >
	RED_INLINE Bool SmartGet( T& val )									{ RED_HALT( "Type not implemented" ); return true; }
	template< class T >
	RED_INLINE Bool SmartGet( red::DynArray< T >& val )
	{
		RED_ASSERT( val.Size() <= 0xffff );
		Uint16 count;
		if ( !Get( count ) )
			return false;
		val.Resize( count );
		if ( count )
		{
			if( !Read( val.TypedData(), static_cast< Uint32 >( val.DataSize() ) ) )
				return false;
		}

		return true;
	}
	template< class T >
	RED_INLINE Bool SmartGetLarge( red::DynArray< T >& val )
	{
		RED_ASSERT( val.Size() <= 0xffffffff );
		Uint32 count;
		if ( !Get( count ) )
			return false;
		val.Resize( count );
		if ( count )
		{
			if( !Read( val.TypedData(), static_cast< Uint32 >( val.DataSize() ) ) )
				return false;
		}

		return true;
	}

	template < class T, class SortPredicate >
	RED_INLINE Bool SmartGet( red::SortedArray< T, SortPredicate >& val )
	{
		return SmartGet( static_cast< red::DynArray< T >& >( val ) );
	}
	template < class T, class SortPredicate >
	RED_INLINE Bool SmartGetLarge( red::SortedArray< T, SortPredicate >& val )
	{
		return SmartGetLarge( static_cast< red::DynArray< T >& >( val ) );
	}


	RED_INLINE Bool Read( void* dataPtr, Uint32 dataSize )
	{
		if ( m_currentPos + dataSize > m_dataSize )
		{
			return false;
		}
		red::Memcpy( dataPtr, &m_data[ m_currentPos ], dataSize );
		m_currentPos += dataSize;
		return true;
	}
	RED_INLINE Bool Skip( Uint32 dataSize )
	{
		if ( m_currentPos + dataSize > m_dataSize )
		{
			return false;
		}
		m_currentPos += dataSize;
		return true;
	}
	RED_INLINE Uint16 GetVersion() const							{ return m_fileVersion; }
	RED_INLINE Uint32 GetCurrentPos() const							{ return m_currentPos; }
	RED_INLINE void SetCurrentPos( Uint32 pos )						{ m_currentPos = pos; }

protected:
	RED_INLINE const void* GetCurrentDataPtr() const				{ return &m_data[ m_currentPos ]; }

	const Int8* const		m_data;
	const Uint32			m_dataSize;

	Uint32					m_currentPos;
	Uint16					m_fileVersion;
};


template<>
RED_INLINE void CSimpleBufferWriter::SmartPut< red::String >( const red::String& val )
{
	RED_ASSERT( val.Length() <= 0xffffffff );
	Uint32 count = Uint32( val.Length() );
	Put( count );
	if ( count )
	{
		Write( val.Data(), static_cast< Uint32 >( val.DataSize() ) );
	}
}	
template<>
RED_INLINE Bool CSimpleBufferReader::SmartGet< red::String >( red::String& val )
{
	RED_ASSERT( val.Length() <= 0xffffffff );
	Uint32 count;
	if ( !Get( count ) )
		return false;
	val.Resize( count );
	if ( count )
	{
		if( !Read( val.Data(), static_cast< Uint32 >( val.DataSize() ) ) )
			return false;
	}

	return true;
}
