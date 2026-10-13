/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../backend/backendEngine/include/exporter.h"
#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/renderData/include/imageProcessing.h"

using red::String;

/// Bitmap texture Exporter based on DirectXTex functionality
class CBitmapExporter : public IExporter
{
	RTTI_DECLARE_TYPE( CBitmapExporter );

public:
	CBitmapExporter();
	virtual Bool DoExport( const ExportOptions& options );

	static GpuWrapApi::eTextureSaveFormat FileExtensionToSaveFormat( const String& extension );
};

RTTI_BEGIN_TYPE( CBitmapExporter );
	RTTI_PARENT_TYPE( IExporter );
RTTI_END_TYPE();


CBitmapExporter::CBitmapExporter()
{
	// Supported class
	m_resourceClass = ClassID< CBitmapTexture >();

	// Supported formats
	m_formats.PushBack( CFileFormat( "dds", "DirectDraw Surface" ) );
	m_formats.PushBack( CFileFormat( "bmp", "Windows Bitmap" ) );
	m_formats.PushBack( CFileFormat( "jpg", "Joint Photographics Experts Group" ) );
	m_formats.PushBack( CFileFormat( "tga", "Truevision Targa" ) );
	m_formats.PushBack( CFileFormat( "png", "Portable Network Graphics" ) );
}

GpuWrapApi::eTextureSaveFormat CBitmapExporter::FileExtensionToSaveFormat( const String& extension )
{
	struct 
	{
		GpuWrapApi::eTextureSaveFormat	m_format;
		char						m_extension[4];
	} Mapper[] = 
	{
		{ GpuWrapApi::SAVE_FORMAT_DDS,	"dds", },
		{ GpuWrapApi::SAVE_FORMAT_BMP,	"bmp", },
		{ GpuWrapApi::SAVE_FORMAT_JPG,	"jpg", },
		{ GpuWrapApi::SAVE_FORMAT_TGA,	"tga", },
		{ GpuWrapApi::SAVE_FORMAT_PNG,	"png", },
	};

	for ( Int32 i = 0; i < RED_ARRAY_COUNT( Mapper ); i ++ )
	{
		if ( !red::Strcmp( Mapper[i].m_extension, extension.AsChar() ) )
		{
			return Mapper[i].m_format;
		}
	}

	return GpuWrapApi::SAVE_FORMAT_PNG;
}

static GpuWrapApi::eTextureFormat GetGpuTextureFormat( ETextureRawFormat format )
{
	static_assert(TRF_Max == 10, "Missing mapping");

	switch ( format )
	{
		case TRF_TrueColor:			return GpuWrapApi::TEXFMT_R8G8B8A8_Unorm;
		case TRF_Grayscale: 		return GpuWrapApi::TEXFMT_L8_Unorm;
		case TRF_Grayscale_Font:	return GpuWrapApi::TEXFMT_A8_Unorm;
		case TRF_HDRFloat:			return GpuWrapApi::TEXFMT_R32G32B32A32_Float;
		case TRF_HDRHalf:			return GpuWrapApi::TEXFMT_R16G16B16A16_Float;
		case TRF_DeepColor:			return GpuWrapApi::TEXFMT_R16G16B16A16_Unorm;
		case TRF_HDRFloatGrayscale: return GpuWrapApi::TEXFMT_R32_Float;
		case TRF_R8G8:				return GpuWrapApi::TEXFMT_R8G8_Unorm;
		case TRF_R32UI:				return GpuWrapApi::TEXFMT_R32_Uint;
		default: break;
	}
 
	return GpuWrapApi::TEXFMT_NULL;
}

bool CBitmapExporter::DoExport( const ExportOptions& options )
{
	ASSERT( options.m_resource->IsA( ClassID<CBitmapTexture>() ));

	// Get bitmap texture resource
	auto bitmapTexture = Cast< CBitmapTexture >( options.m_resource );
	if ( !bitmapTexture->GetSourceData() )
	{
		return false;
	}

	// Get mipmap
	const img::ImagePtr sourceTextureImg = bitmapTexture->GetImage();
	if( !sourceTextureImg )
	{
		return false;
	}

	GpuWrapApi::eTextureFormat format =  GetGpuTextureFormat( bitmapTexture->GetTextureSetup().m_rawFormat );
	GpuWrapApi::eTextureSaveFormat saveFormat = CBitmapExporter::FileExtensionToSaveFormat( options.m_saveFileFormat.GetExtension() );

	GpuWrapApi::TextureDataDesc textureToSave;
	textureToSave.dataPtr		= static_cast<Uint8*>( sourceTextureImg->GetPixelBuffer() );
	textureToSave.slicePitch	= sourceTextureImg->GetPixelBufferSize();
	textureToSave.width			= sourceTextureImg->GetSize().m_width;
	textureToSave.height		= sourceTextureImg->GetSize().m_height;
	textureToSave.format		= format;
	textureToSave.rowPitch		= sourceTextureImg->GetLayout().m_rowPitch;

	return GpuWrapApi::SaveTextureToFile( textureToSave, ANSI_TO_UNICODE( options.m_saveFilePath.AsChar() ), saveFormat );
}
