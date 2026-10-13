/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "bitmapDataBuffer.h"

// DirectX only! (For now, at least, we need an equivalent for Orbis)
// acseparate #include "../gpu dontIncludeFromHere ApiUtils/gpu dontIncludeFromHere ApiMemory.h"
#include "../../redReflection/include/bufferAsyncProxy.h"
#include "../../redReflection/include/deferredDataBuffer.h"

BitmapMipLatentDataBuffer::BitmapMipLatentDataBuffer()
{
	// Default constructor is safe to call into base class
}

BitmapMipLatentDataBuffer::BitmapMipLatentDataBuffer( const void* data, Uint32 size )
{
#if !defined( NO_EDITOR ) || defined( VANGUARD_ASSET_HOST )
	m_loadedData = m_data.Editor_SetContentDirect( data, size );
#else
	RED_FATAL( "Not supported" );
#endif
}

BitmapMipLatentDataBuffer::BitmapMipLatentDataBuffer( Uint32 size )
{
#if !defined( NO_EDITOR ) || defined( VANGUARD_ASSET_HOST )
	m_loadedData = m_data.Editor_SetContentDirect( size );
#else
	RED_FATAL( "Not supported" );
#endif
}

BitmapMipLatentDataBuffer::~BitmapMipLatentDataBuffer()
{
}

void BitmapMipLatentDataBuffer::Serialize( IFile& f )
{
 	m_data.Serialize( f );
}

void BitmapMipLatentDataBuffer::Allocate( const Uint32 size )
{
#if !defined( NO_EDITOR ) || defined( VANGUARD_ASSET_HOST )
	m_loadedData = m_data.Editor_SetContentDirect( size );
#else
	RED_FATAL( "Not supported" );
#endif
}

void* BitmapMipLatentDataBuffer::GetData() const
{
#if !defined( NO_EDITOR ) || defined( VANGUARD_ASSET_HOST )
	if ( !m_loadedData.Data() )
	{
		m_loadedData = m_data.Editor_LoadContentDirect();
	}
#else
	RED_FATAL( "Not supported" );
#endif

	return const_cast<void*>( m_loadedData.Data() );
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
