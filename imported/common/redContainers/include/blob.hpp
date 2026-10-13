/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace red
{
	//-----------------------------------------------------------------------------
	// BlobSpan

	constexpr BlobSpan::BlobSpan()
		: m_data( nullptr )
		, m_size( 0 )
	{
	}

	constexpr BlobSpan::BlobSpan( void* data, size_t size )
		: m_data( data )
		, m_size( static_cast<Uint32>( size ) )
	{
	}

	constexpr BlobSpan::BlobSpan( void* startPtr, void* endPtr )
		: m_data( startPtr )
		, m_size( static_cast<Uint32>( static_cast<uint8_t*>( endPtr ) - static_cast<uint8_t*>( startPtr ) ) )
	{
	}

	RED_INLINE BlobSpan::BlobSpan( Blob& other )
		: m_data( other.Data() )
		, m_size( other.Size() )
	{
	}

	template <typename T>
	constexpr BlobSpan BlobSpan::OfItem( T& item )
	{
		return BlobSpan( &item, sizeof(T) );
	}

	constexpr bool BlobSpan::Empty() const
	{
		return m_data == nullptr || m_size == 0;
	}

	constexpr Uint32 BlobSpan::Size() const
	{
		return m_size;
	}

	RED_INLINE Uint8& BlobSpan::operator [] ( Uint32 offset ) const
	{
		CheckRange( offset, 1 );
		return *static_cast<Uint8*>( Data( offset ) );
	}

	constexpr void* BlobSpan::Data( Uint32 offset ) const
	{
		return static_cast<Uint8*>( m_data ) + offset;
	}

	template <typename T>
	RED_INLINE T* BlobSpan::Pointer( Uint32 offset ) const
	{
		CheckRange( offset, sizeof(T) );
		return static_cast< T* >( Data( offset ) );
	}

	template <typename T>
	RED_INLINE T& BlobSpan::As( Uint32 offset ) const
	{
		return *Pointer<T>( offset );
	}

	template <typename T>
	RED_INLINE ArraySpan< T > BlobSpan::AsArray( Uint32 offset, Uint32 count ) const
	{
		CheckRange( offset, count * sizeof( T ) );
		return ArraySpan< T >( Pointer< T >( offset ), count );
	}

	RED_INLINE BlobSpan BlobSpan::Range( Uint32 offset, Uint32 size )
	{
		CheckRange( offset, size );
		return BlobSpan( Data( offset ), size );
	}

	RED_INLINE BlobSpan BlobSpan::Range( Uint32 offset )
	{
		CheckRange( offset, m_size - offset );
		return BlobSpan( Data( offset ), m_size - offset );
	}

	RED_INLINE BlobSpan MakeBlobSpan( const UniqueBuffer& buffer )
	{
		return BlobSpan( buffer.Get(), buffer.GetSize() );
	}

	RED_INLINE BlobSpan MakeBlobSpan( red::DynArray< Uint8 >& buffer )
	{
		return BlobSpan( buffer.Data(), buffer.DataSize() );
	}


	//-----------------------------------------------------------------------------
	// BlobView

	constexpr BlobView::BlobView()
		: m_data( nullptr )
		, m_size( 0 )
	{
	}

	constexpr BlobView::BlobView( const void* data, size_t size )
		: m_data( data )
		, m_size( static_cast<Uint32>( size ) )
	{
	}

	constexpr BlobView::BlobView( const void* startPtr, const void* endPtr )
		: m_data( startPtr )
		, m_size( static_cast<Uint32>( static_cast<const uint8_t*>(endPtr) - static_cast<const uint8_t*>(startPtr) ) )
	{
	}

	constexpr BlobView::BlobView( const BlobSpan& other )
		: m_data( other.Data() )
		, m_size( other.Size() )
	{
	}

	RED_INLINE BlobView::BlobView( const Blob& other )
		: m_data( other.Data() )
		, m_size( other.Size() )
	{
	}

	template <typename T>
	constexpr BlobView BlobView::OfItem( const T& item )
	{
		return BlobView( &item, sizeof(T) );
	}

	RED_INLINE BlobView& BlobView::operator = ( const BlobSpan& other )
	{
		m_data = other.Data();
		m_size = other.Size();
		return *this;
	}

	constexpr bool BlobView::Empty() const
	{
		return m_data == nullptr || m_size == 0;
	}

	constexpr Uint32 BlobView::Size() const
	{
		return m_size;
	}

	RED_INLINE const Uint8& BlobView::operator [] ( Uint32 offset ) const
	{
		CheckRange( offset, 1 );
		return *static_cast<const Uint8*>( Data( offset ) );
	}

	constexpr const void* BlobView::Data( Uint32 offset ) const
	{
		return static_cast<const Uint8*>( m_data ) + offset;
	}

	template <typename T>
	RED_INLINE const T* BlobView::Pointer( Uint32 offset ) const
	{
		CheckRange( offset, sizeof(T) );
		return static_cast< const T* >( Data( offset ) );
	}

	template <typename T>
	RED_INLINE const T& BlobView::As( Uint32 offset ) const
	{
		return *Pointer<T>( offset );
	}

	template <typename T>
	RED_INLINE ArraySpan< const T > BlobView::AsArray( Uint32 offset, Uint32 count ) const
	{
		CheckRange( offset, count * sizeof( T ) );
		return ArraySpan< const T >( static_cast< const T* >( Data( offset ) ), count );
	}

	RED_INLINE BlobView BlobView::Range( Uint32 offset, Uint32 size ) const
	{
		CheckRange( offset, size );
		return BlobView( Data( offset ), size );
	}

	RED_INLINE BlobView BlobView::Range( Uint32 offset ) const
	{
		CheckRange( offset, m_size - offset );
		return BlobView( Data( offset ), m_size - offset );
	}

	RED_INLINE BlobView MakeBlobView( const UniqueBuffer & buffer )
	{
		return BlobView( buffer.Get(), buffer.GetSize() );
	}

	RED_INLINE BlobView MakeBlobView(const io::ShareableIOMemory& buffer)
	{
		return MakeBlobView( buffer.GetUnderlyingBuffer() );
	}

	RED_INLINE BlobView MakeBlobView( const red::DynArray< Uint8 >& buffer )
	{
		return BlobView( buffer.Data(), buffer.DataSize() );
	}

	//-----------------------------------------------------------------------------
	// Blob

	RED_INLINE Blob::Blob( red::UniqueBuffer&& buffer )
		: m_buffer( std::move( buffer ) )
	{
	}

	constexpr bool Blob::Empty() const
	{
		return !m_buffer;
	}

	constexpr Uint32 Blob::Size() const
	{
		return m_buffer.GetSize();
	}

	RED_INLINE Uint8& Blob::operator [] ( Uint32 offset )
	{
		CheckRange(offset, 1);
		return *static_cast<Uint8*>( Data( offset ) );
	}

	RED_INLINE const Uint8& Blob::operator [] ( Uint32 offset ) const
	{
		CheckRange(offset, 1);
		return *static_cast<const Uint8*>( Data( offset ) );
	}

	RED_INLINE void* Blob::Data()
	{
		return m_buffer.Get();
	}

	RED_INLINE const void* Blob::Data() const
	{
		return m_buffer.Get();
	}

	RED_INLINE BlobSpan Blob::Span()
	{
		return BlobSpan( Data(), Size() );
	}

	RED_INLINE BlobView Blob::View() const
	{
		return BlobView( Data(), Size() );
	}

	RED_INLINE BlobSpan Blob::Span( Uint32 offset, Uint32 size )
	{
		CheckRange( offset, size );
		return BlobSpan( Data( offset ), size );
	}

	RED_INLINE BlobView Blob::View( Uint32 offset, Uint32 size ) const
	{
		CheckRange( offset, size );
		return BlobView( Data( offset ), size );
	}

	template <typename T>
	RED_INLINE T* Blob::Pointer( Uint32 offset )
	{
		CheckRange( offset, sizeof(T) );
		return static_cast< T* >( Data( offset ) );
	}

	template <typename T>
	RED_INLINE const T* Blob::Pointer( Uint32 offset ) const
	{
		CheckRange( offset, sizeof(T) );
		return static_cast< const T* >( Data( offset ) );
	}

	template <typename T>
	RED_INLINE T& Blob::As( Uint32 offset )
	{
		return *Pointer<T>( offset );
	}

	template <typename T>
	RED_INLINE const T& Blob::As( Uint32 offset ) const
	{
		return *Pointer<T>( offset );
	}

	template <typename T>
	RED_INLINE ArraySpan< T > Blob::AsArray( Uint32 offset, Uint32 count )
	{
		CheckRange( offset, count * sizeof( T ) );
		return ArraySpan< T >( Pointer< T >( offset ), count );
	}

	template <typename T>
	RED_INLINE ArraySpan< const T > Blob::AsArray( Uint32 offset, Uint32 count ) const
	{
		CheckRange( offset, count * sizeof( T ) );
		return ArraySpan< const T >( Pointer< const T >( offset ), count );
	}

	RED_INLINE BlobSpan Blob::Range( Uint32 offset, Uint32 size )
	{
		CheckRange( offset, size );
		return BlobSpan( Data( offset ), size );
	}

	RED_INLINE BlobView Blob::Range( Uint32 offset, Uint32 size ) const
	{
		CheckRange( offset, size );
		return BlobView( Data( offset ), size );
	}

} // namespace red
