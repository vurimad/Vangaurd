/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "bitmapDataBuffer.h"
#include "../../renderData/include/textureEnums.h"
#include "../../redReflection/include/resource.h"

namespace img
{
	enum class PixelFormat : Uint8;
	class Image;
	typedef red::SharedPtr<Image> ImagePtr;
}

class RESOURCEMATERIAL_API CSourceTexture : public ISerializable
{
	friend class CBitmapTexture;
	friend class CCubeTexture;

	RTTI_DECLARE_TYPE( CSourceTexture );
	RED_BASE_CLASS( ISerializable );

private:
	mutable BitmapMipLatentDataBuffer	m_dataBuffer;
	Uint32								m_width;
	Uint32								m_height;
	Uint32								m_depth;
	Uint32								m_pitch;
	ETextureRawFormat					m_format;

public:
	CSourceTexture();
	~CSourceTexture();

public:

	Uint32 GetWidth() const { return m_width; }
	Uint32 GetHeight() const { return m_height; }
	Uint32 GetDepth() const { return m_depth; }
	ETextureRawFormat GetFormat() const { return m_format; }

	img::PixelFormat MapRawFormatToPixelFormat( ETextureRawFormat format ) const;

	img::ImagePtr GetImage() const;
	void SetImage( const img::ImagePtr & image );
	img::ImagePtr GetSubImage( Uint32 x, Uint32 y, Uint32 width, Uint32 height ) const;

	// Initialize empty
	void Init( Uint32 width, Uint32 height, Uint32 depth, ETextureRawFormat format );

	Uint32 GetBufferSize() const { return m_dataBuffer.GetSize(); }

	// Create from raw data with same format, size and no compression
	Bool CreateFromRawData( const void* buf, Uint32 origWidth, Uint32 origHeight, Uint32 origDepth, Uint32 origPitch );

	// Create from mip with same format, size, but with compression - use only in fallback cases :( 
	Bool CreateFromDataCompressed( const void* data, size_t dataSize, ETextureCompression textureCompression );

	// On serialize
	virtual void OnSerialize( IFile& file ) override;

public:
	// Helper function to copy raw buffer assuming that it has same compression and format
	static void CopyBufferPitched( void* dest, Uint32 destPitch, const void* src, Uint32 srcPitch, Uint32 rowDataSize, Uint32 numRows, Uint32 sliceDataOffset );
};

