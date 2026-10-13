/*
 * Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
 */

#if defined( VANGUARD_ASSET_HOST )
#include <limits>
#include "../../../backend/backendMesh/src/build.h"
#else
#include "build.h"
#endif
#include "../include/renderMeshBufferWriter.h"


CRenderMeshBufferWriter::CRenderMeshBufferWriter( GrowableMemoryBuffer& buffer )
	: m_buffer( &buffer )
{
}

RED_INLINE void CRenderMeshBufferWriter::Write( const void* data, Uint32 size )
{
	// Always write at the end of the buffer
	m_buffer->Write( m_buffer->Size(), data, size );
}

Uint32 CRenderMeshBufferWriter::GetOffset() const
{
	return m_buffer->Size();
}

void CRenderMeshBufferWriter::Align( Uint32 size )
{
	if ( size > 1 )
	{
		Uint32 baseOffset = GetOffset();
		Uint32 offset = GetOffset();

		offset += (size-1);
		offset -= ( offset % size );

		while ( offset > baseOffset )
		{
			Uint8 zero = 0;
			Write( &zero, sizeof(zero) );
			baseOffset += 1;
		}
	}
}

void CRenderMeshBufferWriter::WriteFloat( Float data )
{
	Write( &data, sizeof(data) );
}

void CRenderMeshBufferWriter::WriteFloats( const Float* data, Uint32 numFloats )
{
	Write( data, numFloats * sizeof( Float ) );
}

void CRenderMeshBufferWriter::WriteUnsignedByte( Uint8 data )
{
	Write( &data, sizeof(data) );
}

void CRenderMeshBufferWriter::WriteUnsignedByteN( Float data )
{
	if ( data <= 0.0f )
	{
		WriteUnsignedByte( 0 );
	}
	else if ( data >= 1.0f )
	{
		WriteUnsignedByte( 255 );
	}
	else
	{
		Uint8 val = static_cast<Uint8>( data * 255.0f );
		Write( &val, sizeof(val) );
	}
}

void CRenderMeshBufferWriter::WriteSignedByteN( Float data )
{
	if ( data <= -1.0f )
	{
		WriteUnsignedByte( 0 );
	}
	else if ( data >= 1.0f )
	{
		WriteUnsignedByte( 255 );
	}
	else
	{
		Uint8 val = static_cast<Uint8>( 127.0f + data * 127.0f );
		Write( &val, sizeof(val) );
	}
}

void CRenderMeshBufferWriter::WriteUnsignedShort( Uint16 data )
{
	Write( &data, sizeof(data) );
}

void CRenderMeshBufferWriter::WriteUnsignedShorts( const Uint16* data, Uint32 numShorts )
{
	Write( data, numShorts * sizeof( Uint16 ) );
}

void CRenderMeshBufferWriter::WriteUnsignedShortN( Float data )
{
	Uint16 val;
	if ( data <= 0.0f )
	{
		val = 0;
	}
	else if ( data >= 1.0f )
	{
		val = std::numeric_limits< Uint16 >::max();
	}
	else
	{
		val = static_cast<Uint16>( data * std::numeric_limits< Uint16 >::max() );
	}

	Write( &val, sizeof( val ) );
}

void CRenderMeshBufferWriter::WriteUnsignedShortsN( const Float* data, Uint32 numFloats )
{
	Uint16* temp = reinterpret_cast<Uint16*>( RED_ALLOCA( numFloats * sizeof( Uint16 ) ) );

	for ( Uint32 i = 0; i < numFloats; ++i )
	{
		Float value = *(data++);
		if ( value <= 0.0f )
		{
			temp[i] = 0;
		}
		else if ( value >= 1.0f )
		{
			temp[i] = std::numeric_limits< Uint16 >::max();
		}
		else
		{
			temp[i] = static_cast<Uint16>(value * std::numeric_limits< Uint16 >::max());
		}
	}

	Write( temp, sizeof( Uint16 ) * numFloats );
}

void CRenderMeshBufferWriter::WriteSignedShortN( Float data )
{
	// range is symmetric, -32767 - 32767, rather than -32768 - 32767.

	Int16 val;
	if ( data <= -1.0f )
	{
		val = -std::numeric_limits< Int16 >::max();
	}
	else if ( data >= 1.0f )
	{
		val = std::numeric_limits< Int16 >::max();
	}
	else
	{
		val = static_cast<Int16>(data * std::numeric_limits< Int16 >::max());
	}

	Write( &val, sizeof( val ) );
}

void CRenderMeshBufferWriter::WriteSignedShortsN( const Float* data, Uint32 numFloats )
{
	// range is symmetric, -32767 - 32767, rather than -32768 - 32767.

	Int16* temp = reinterpret_cast<Int16*>(RED_ALLOCA( numFloats * sizeof( Int16 ) ));

	for ( Uint32 i = 0; i < numFloats; ++i )
	{
		Float value = *(data++);
		if ( value <= -1.0f )
		{
			temp[i] = -std::numeric_limits< Int16 >::max();
		}
		else if ( value >= 1.0f )
		{
			temp[i] = std::numeric_limits< Int16 >::max();
		}
		else
		{
			temp[i] = static_cast<Int16>(value * std::numeric_limits< Int16 >::max());
		}
	}

	Write( temp, sizeof( Int16 ) * numFloats );
}


void CRenderMeshBufferWriter::WriteUnsignedInt( Uint32 data )
{
	Write( &data, sizeof(data) );
}

void CRenderMeshBufferWriter::WriteFloat16( Float f )
{
	Uint16 data = Float16Compressor::Compress( f );
	Write( &data, sizeof(data) );
}

void CRenderMeshBufferWriter::WriteFloats16( const Float* data, Uint32 numFloats )
{
	Uint16* temp = reinterpret_cast<Uint16*>( RED_ALLOCA( numFloats * sizeof( Uint16 ) ) );

	for ( Uint32 i = 0; i < numFloats; ++i )
	{
		temp[i] = Float16Compressor::Compress( *(data++) );
	}

	Write( temp, sizeof( Uint16 ) * numFloats );
}

void CRenderMeshBufferWriter::WriteColor( Uint32 data )
{
	Write( &data, sizeof(data) );
}

void CRenderMeshBufferWriter::WriteDec3N( Float x, Float y, Float z )
{
	int vx = (int) ::Clamp < Float >( x * 1023.0f, 0.0f, 1023.0f );
	int vy = (int) ::Clamp < Float >( y * 1023.0f, 0.0f, 1023.0f );
	int vz = (int) ::Clamp < Float >( z * 1023.0f, 0.0f, 1023.0f );
	int vw = 1;
	Uint32 data = ((vx&1023)+((vy&1023)<<10)+((vz&1023)<<20)+((vw&3)<<30));
	Write( &data, sizeof(data) );
}

void CRenderMeshBufferWriter::WriteDec4N( Float x, Float y, Float z, Float w )
{
	int vx = (int) ::Clamp < Float >( x * 1023.0f, 0.0f, 1023.0f );
	int vy = (int) ::Clamp < Float >( y * 1023.0f, 0.0f, 1023.0f );
	int vz = (int) ::Clamp < Float >( z * 1023.0f, 0.0f, 1023.0f );
	int vw = (int) ::Clamp < Float >( w * 3.0f, 0.0f, 3.0f );
	Uint32 data = ((vx&1023)+((vy&1023)<<10)+((vz&1023)<<20)+((vw&3)<<30));
	Write( &data, sizeof(data) );
}
