/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{
	class RED_REFLECTION_API PackageStream
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public: 
		PackageStream();
		virtual ~PackageStream();
	
		RED_MOCKABLE Uint64 Read( void * data, Uint64 size );
		RED_MOCKABLE Uint64 Write( const void * data, Uint64 size );
	
		void Seek( Uint64 position );
		void Skip( Uint64 value );

		Uint64 GetPosition() const;

	private:

		virtual Uint64 OnRead( void * data, Uint64 size, Uint64 position ) = 0;
		virtual Uint64 OnWrite( const void * data, Uint64 size, Uint64 position ) = 0;

		virtual Uint64 OnSeek( Uint64 positionRequest ) = 0;

		Uint64 m_position;
	};

	RED_INLINE void PackageStream::Seek( Uint64 position )
	{
		m_position = OnSeek( position );
	}

	RED_INLINE void PackageStream::Skip( Uint64 value )
	{
		m_position = OnSeek( m_position + value );
	}

	RED_INLINE Uint64 PackageStream::GetPosition() const
	{
		return m_position;
	}

	RED_INLINE Uint64 PackageStream::Read( void* data, Uint64 size )
	{
		RED_FATAL_ASSERT( data, "Cannot Read to a null buffer." );

		const Uint64 read = OnRead( data, size, m_position );
		m_position += read;
		return m_position;
	}

	RED_INLINE Uint64 PackageStream::Write( const void* data, Uint64 size )
	{
		if( size )
		{
			RED_FATAL_ASSERT( data, "Cannot Write from a null buffer." );
			const Uint64 written = OnWrite( data, size, m_position );
			m_position += written;
		}

		return m_position;
	}
}
