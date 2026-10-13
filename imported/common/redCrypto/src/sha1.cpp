/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "sha1.h"
#include "sha1prv.h"

namespace crypto
{
void CalculateSha1_Insecure( const void* buffer, const red::Uint64 size, Ssha1& outHash )
{
	sha1::SHA1_CTX context;
	sha1::SHA1_Init( &context );
	sha1::SHA1_Update( &context, static_cast< const Uint8* >( buffer ), size );
	sha1::SHA1_Final( &context, outHash.m_value );
}


static sha1::SHA1_CTX* CastContext( Uint8* buffer )
{
	return reinterpret_cast< sha1::SHA1_CTX* >( buffer );
}

Sha1Computer::Sha1Computer()
{
	static_assert( sizeof( sha1::SHA1_CTX ) == sizeof( m_contextBuffer ), "Context size must match crypto context size!" );
	sha1::SHA1_Init( CastContext( m_contextBuffer ) );
}

void Sha1Computer::PushData( const void* buffer, const red::Uint64 size )
{
	sha1::SHA1_Update( CastContext( m_contextBuffer ), static_cast< const Uint8* >( buffer ), size );
}

Ssha1 Sha1Computer::Finalise()
{
	Ssha1 result;
	sha1::SHA1_Final( CastContext( m_contextBuffer ), result.m_value );
	return result;
}

} // crypto

