/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redMemory/include/uniqueBuffer.h"
#include "../../../common/redIO/include/redIOCommon.h"
#include "arraySpan.h"

namespace red
{
	class Blob;

	//-----------------------------------------------------------------------------
	// Represents a span of data stored in a Blob class
	// This view can be used for reading and writing the data
	// Does not resize the buffer or allow access past the size
	class BlobSpan
	{
	public:
		constexpr BlobSpan();
		constexpr BlobSpan( void* data, size_t size );
		constexpr BlobSpan( void* startPtr, void* endPtr );
		constexpr BlobSpan( const BlobSpan& ) = default;
		BlobSpan( Blob& other );
		~BlobSpan() = default;

		template <typename T>
		constexpr static BlobSpan OfItem( T& item );

		BlobSpan& operator = ( const BlobSpan& ) = default;

		constexpr bool Empty() const;
		constexpr Uint32 Size() const;
		constexpr Uint32 MaxSize() const { return std::numeric_limits< Uint32 >::max(); }

		Uint8& operator [] ( Uint32 offset ) const;

		constexpr void* Data( Uint32 offset = 0 ) const;

		template <typename T>
		T* Pointer( Uint32 offset = 0 ) const;

		template <typename T>
		T& As( Uint32 offset = 0 ) const;

		template <typename T>
		ArraySpan< T > AsArray( Uint32 offset, Uint32 count ) const;

		BlobSpan Range( Uint32 offset, Uint32 size );
		BlobSpan Range( Uint32 offset );

	private:
		RED_CONTAINERS_API void CheckRange( Uint32 offset, Uint32 size ) const;

		void* m_data;
		Uint32 m_size;
	};

	BlobSpan MakeBlobSpan( const UniqueBuffer& buffer );
	BlobSpan MakeBlobSpan( const red::DynArray< Uint8 >& buffer );

	//-----------------------------------------------------------------------------
	// A read only view of data stored in a Blob class
	// This view can only be used for reading the data
	// Does not resize the buffer or allow reading past the end of the size
	class BlobView
	{
	public:
		constexpr BlobView();
		constexpr BlobView( const void* data, size_t size );
		constexpr BlobView( const void* startPtr, const void* endPtr );
		constexpr BlobView( const BlobView& ) = default;
		constexpr BlobView( const BlobSpan& other );
		BlobView( const Blob& other );
		~BlobView() = default;

		template <typename T>
		constexpr static BlobView OfItem( const T& item );

		BlobView& operator = ( const BlobView& ) = default;
		BlobView& operator = ( const BlobSpan& other );

		constexpr bool Empty() const;
		constexpr Uint32 Size() const;
		constexpr Uint32 MaxSize() const { return std::numeric_limits< Uint32 >::max(); }

		const Uint8& operator [] ( Uint32 offset ) const;

		constexpr const void* Data( Uint32 offset = 0 ) const;

		template <typename T>
		const T* Pointer( Uint32 offset = 0 ) const;

		template <typename T>
		const T& As( Uint32 offset = 0 ) const;

		template <typename T>
		ArraySpan< const T > AsArray( Uint32 offset, Uint32 count ) const;

		BlobView Range( Uint32 offset, Uint32 size ) const;
		BlobView Range( Uint32 offset ) const;

	private:
		RED_CONTAINERS_API void CheckRange( Uint32 offset, Uint32 size ) const;

		const void* m_data;
		Uint32 m_size;
	};

	BlobView MakeBlobView( const UniqueBuffer& buffer );
	BlobView MakeBlobView( const io::ShareableIOMemory& buffer);
	BlobView MakeBlobView( const red::DynArray< Uint8 >& buffer );

	//-----------------------------------------------------------------------------
	// A simple data buffer class that adds extra accessor functions
	// and provides a way to interact using BlobSpan and BlobView declared above
	class Blob
	{
	public:
		RED_CONTAINERS_API Blob();
		RED_CONTAINERS_API explicit Blob( Uint32 size, Uint32 align = 1 );
		explicit Blob( red::UniqueBuffer&& buffer );
		Blob( Blob&& other ) = default;
		Blob( const Blob& other ) = delete;
		~Blob() = default;

		Blob& operator = ( Blob&& other ) = default;
		Blob& operator = ( const Blob& other ) = delete;

		constexpr bool Empty() const;
		constexpr Uint32 Size() const;
		constexpr Uint32 MaxSize() const { return std::numeric_limits< Uint32 >::max() & ~( m_buffer.GetAlignment() - 1 ); }

		Uint8& operator [] ( Uint32 offset );
		const Uint8& operator [] ( Uint32 offset ) const;

		void* Data();
		const void* Data() const;
		RED_CONTAINERS_API void* Data( Uint32 offset );
		RED_CONTAINERS_API const void* Data( Uint32 offset ) const;

		BlobSpan Span();
		BlobView View() const;
		BlobSpan Span( Uint32 offset, Uint32 size );
		BlobView View( Uint32 offset, Uint32 size ) const;

		template <typename T>
		T* Pointer( Uint32 offset = 0 );
		template <typename T>
		const T* Pointer( Uint32 offset = 0 ) const;

		template <typename T>
		T& As( Uint32 offset = 0 );
		template <typename T>
		const T& As( Uint32 offset = 0 ) const;

		template <typename T>
		ArraySpan< T > AsArray( Uint32 offset, Uint32 count );
		template <typename T>
		ArraySpan< const T > AsArray( Uint32 offset, Uint32 count ) const;

		BlobSpan Range( Uint32 offset, Uint32 size );
		BlobView Range( Uint32 offset, Uint32 size ) const;

		RED_CONTAINERS_API void Resize( Uint32 size );
		RED_CONTAINERS_API red::UniqueBuffer Release();

	private:
		RED_CONTAINERS_API void CheckRange( Uint32 offset, Uint32 size ) const;

		red::UniqueBuffer m_buffer;
	};
} // namespace red

#include "blob.hpp"
