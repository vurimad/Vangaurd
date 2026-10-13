/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

// fucked up includes until tings get cleaned up in engine project -.-"
#include "build.h"
#include "sourceTextureData.h"
#include "../../redImage/include/image.h"

RTTI_BEGIN_TYPE( CSourceTexture );
RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_width ).readOnly();
	RTTI_PROPERTY( m_height ).readOnly();
	RTTI_PROPERTY( m_depth ).readOnly();
	RTTI_PROPERTY( m_pitch ).readOnly();
	RTTI_PROPERTY( m_format ).readOnly();
RTTI_END_TYPE();


CSourceTexture::CSourceTexture()
	: m_width( 0 )
	, m_height( 0 )
	, m_depth( 1 )
	, m_format( TRF_TrueColor )
{
}

CSourceTexture::~CSourceTexture()
{
}

void CSourceTexture::OnSerialize( IFile& file )
{
	TBaseClass::OnSerialize( file );

	// No crap is done on mapping or on GC
	if ( file.IsMapper() )
	{
		return;
	}

	if ( file.IsReader() )
	{
		if ( m_pitch == 0 )
		{
			m_pitch = m_width * Texture::GetPixelSizeInBytes( m_format );
		}
	}

	// Do not serialize during cook
	if( !file.IsCooker() )
	{
		m_dataBuffer.Serialize( file );
	}
}

void CSourceTexture::CopyBufferPitched( void* dest, Uint32 destPitch, const void* src, Uint32 srcPitch, Uint32 rowDataSize, Uint32 numRows, Uint32 sliceDataOffset )
{
	const Uint8* srcPtr = ( const Uint8* ) src + sliceDataOffset;
	Uint8* destPtr = ( Uint8* ) dest + sliceDataOffset;
	for ( Uint32 i=0; i<numRows; i++, srcPtr += srcPitch, destPtr += destPitch )
	{
		red::Memcpy( destPtr, srcPtr, rowDataSize );
	}
}

Bool CSourceTexture::CreateFromRawData( const void* buf, Uint32 origWidth, Uint32 origHeight, Uint32 origDepth, Uint32 origPitch )
{
#ifndef NO_TEXTURE_EDITING
		RED_ASSERT( origHeight <= m_height );
		RED_ASSERT( origWidth <= m_width );
		RED_ASSERT( origDepth <= m_depth );

	Uint32 sourceWidth = origWidth * Texture::GetPixelSizeInBytes(m_format);
	Uint32 rowDataSize = Min<Uint32>(m_pitch, sourceWidth);
	Uint32 numRows = Min<Uint32>(m_height, origHeight) *  Min<Uint32>( m_depth, origDepth );
	CopyBufferPitched( m_dataBuffer.GetData(), m_pitch, buf, origPitch, rowDataSize, numRows, 0 );

	return true;

#else
	return false;
#endif
}

Bool CSourceTexture::CreateFromDataCompressed( const void * data, size_t dataSize, ETextureCompression textureCompression )
{
	RED_FATAL( "Not implemented!" );
	return true;
}

void CSourceTexture::Init( Uint32 width, Uint32 height, Uint32 depth, ETextureRawFormat format )
{
	m_width = width;
	m_height = height;
	m_depth = depth;
	m_format = format;
	m_pitch = m_width * Texture::GetPixelSizeInBytes( format );
	m_dataBuffer.Allocate( m_depth * m_height * m_pitch );
}

img::PixelFormat CSourceTexture::MapRawFormatToPixelFormat( ETextureRawFormat rawFormat ) const
{
	static_assert( TRF_Max == 10, "Add missing mapping" );

	switch ( rawFormat )
	{
	case TRF_TrueColor:			return img::PixelFormat::Format_Uint8;
	case TRF_DeepColor:			return img::PixelFormat::Format_Uint16;
	case TRF_Grayscale:			return img::PixelFormat::Format_Uint8;
	case TRF_Grayscale_Font:	return img::PixelFormat::Format_Uint8;
	case TRF_HDRHalf:			return img::PixelFormat::Format_Float16;
	case TRF_HDRFloat:			return img::PixelFormat::Format_Float32;
	case TRF_HDRFloatGrayscale: return img::PixelFormat::Format_Float32;
	case TRF_R8G8:				return img::PixelFormat::Format_Uint8;
	case TRF_R32UI:				return img::PixelFormat::Format_Uint32;
	}

	RED_HALT( "Unknown texture raw format" );
	return img::PixelFormat::Format_Uint8;
}

img::ImagePtr CSourceTexture::GetImage() const
{
	if ( !m_dataBuffer.Load() )
	{
		RED_HALT( "Unable to load data buffer to generate mipmaps" );
		return img::ImagePtr();
	}

	if ( m_format == TRF_Invalid || m_format >= TRF_Max )
	{
		return img::ImagePtr();
	}

	img::Image::SizeInfo sizeInfo;
	sizeInfo.m_channels = Texture::GetNumChannels( m_format );
	sizeInfo.m_width = m_width;
	sizeInfo.m_height = m_height;
	sizeInfo.m_depth = m_depth;
	img::ImagePtr imagePtr( img::Image::CreateEmpty( MapRawFormatToPixelFormat( m_format ), sizeInfo ) );
	red::Memcpy( imagePtr->GetPixelBuffer(), m_dataBuffer.GetData(), m_dataBuffer.GetSize() );

	m_dataBuffer.Unload();

	return imagePtr;
}

void CSourceTexture::SetImage( const img::ImagePtr & imagePtr )
{
	img::Image::SizeInfo currentSize;
	currentSize.m_channels = Texture::GetNumChannels( m_format );
	currentSize.m_width = m_width;
	currentSize.m_height = m_height;
	currentSize.m_depth = 1;

	const img::Image::SizeInfo & imageSize = imagePtr->GetSize();

	RED_FATAL_ASSERT( currentSize == imageSize && MapRawFormatToPixelFormat(m_format) == imagePtr->GetPixelFormat(), "Only image of same size and same format can set." );
	
	m_dataBuffer.Allocate( m_height * m_pitch );

	if ( imagePtr->GetPixelBufferSize() == m_dataBuffer.GetSize() )
	{
		red::Memcpy( m_dataBuffer.GetData(), imagePtr->GetPixelBuffer(), imagePtr->GetPixelBufferSize() );
	}
	else
	{
		const Uint32 destRowPitch = m_pitch;
		const Uint32 srcRowPitch = imagePtr->GetLayout().m_rowPitch;
		RED_ASSERT( destRowPitch < srcRowPitch, "Expect src (%u) to be padded up compared to dest (%u)", srcRowPitch, destRowPitch );

		void* destPixels = m_dataBuffer.GetData();
		const void* srcPixels = imagePtr->GetPixelBuffer();

		for ( Uint32 y = 0; y < m_height; ++y )
		{
			red::Memcpy( destPixels, srcPixels, srcRowPitch );
			destPixels = red::OffsetPtr( destPixels, destRowPitch );
			srcPixels = red::OffsetPtr( srcPixels, srcRowPitch );
		}
	}
}

img::ImagePtr CSourceTexture::GetSubImage( Uint32 x, Uint32 y, Uint32 width, Uint32 height ) const
{
	if( x + width > m_width || y + height > m_height )
	{
		RED_LOG_WARNING( "Engine: Invalid subregion of the image." );
		return img::ImagePtr();
	}

	if ( !m_dataBuffer.Load() )
	{
		RED_LOG_WARNING( "Engine: Unable to load data buffer to generate mipmaps" );
		return img::ImagePtr();
	}

	img::Image::SizeInfo sizeInfo;
	sizeInfo.m_channels = Texture::GetNumChannels( m_format );
	sizeInfo.m_width = width;
	sizeInfo.m_height = height;
	sizeInfo.m_depth = 1;
	img::ImagePtr imagePtr( img::Image::CreateEmpty( MapRawFormatToPixelFormat( m_format ), sizeInfo ) );

	Uint32 pixelSize = Texture::GetPixelSizeInBytes( m_format );
	Uint32 ptrOffset = pixelSize * ( x + y * m_width );
	Uint32 srcRowPitch = pixelSize * m_width;
	Uint32 dstRowPitch = pixelSize * width;	

	const Uint8* srcPtr = ( const Uint8* ) red::OffsetPtr( m_dataBuffer.GetData(), ptrOffset );
	Uint8* destPtr = ( Uint8* )imagePtr->GetPixelBuffer();
	for ( Uint32 i=0; i<height; i++, srcPtr += srcRowPitch, destPtr += dstRowPitch )
	{
		red::Memcpy( destPtr, srcPtr, dstRowPitch );
	}

	m_dataBuffer.Unload();

	return imagePtr;
}
