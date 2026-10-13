/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../backend/backendEngine/include/importer.h"
#include "../../../common/renderBackend/include/renderTextureCompiler.h"
#include "../../../common/renderData/include/imageProcessing.h"
#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../../common/resourceMaterial/include/cubeTexture.h"
#include "../../../common/redReflection/include/util.h"
#include "cubeTextureCooker.h"

using red::DynArray;

/// Cubemap texture importer based on DirectXTex functionality
class CCubemapImporter : public IImporter
{
	RTTI_DECLARE_TYPE( CCubemapImporter );

public:
	CName					m_textureGroup;

public:
	CCubemapImporter();

public:
	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options ) override;
};

RTTI_BEGIN_TYPE( CCubemapImporter )
	RTTI_PARENT_TYPE(IImporter)
	RTTI_PROPERTY( m_textureGroup ).customEditor( "TextureGroupList" ).editable();
RTTI_END_TYPE()



CCubemapImporter::CCubemapImporter()
{
	// Supported class
	m_resourceClass = ClassID< CCubeTexture >();
	m_textureGroup = RED_NAME_CONSTEXPR( "WorldDiffuse" );

	// Supported formats
	m_formats.PushBack( CFileFormat( "dds", "DirectDraw Surface" ) );
	m_formats.PushBack( CFileFormat( "bmp", "Windows Bitmap" ) );
	m_formats.PushBack( CFileFormat( "jpg", "Joint Photographics Experts Group" ) );
	m_formats.PushBack( CFileFormat( "tga", "Truevision Targa" ) );
	m_formats.PushBack( CFileFormat( "png", "Portable Network Graphics" ) );
	m_formats.PushBack( CFileFormat( "exr", "OpenEXR" ) );
}

ImportResult CCubemapImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options )
{
	// Get the target object
	THandle< CCubeTexture > resource = Cast< CCubeTexture >( inputResource );
	if ( !resource )
	{
		return ImportResult::Error( "Core: Unable to import cubemap into something that is not a cubetexture" );
	}

	red::UniqueBuffer srcData;
	GpuWrapApi::TextureDataDesc desc;

	const red::AbsolutePath sourceFilePath = options->GetSourcePath();
	if ( !red::utils::HasExtension( sourceFilePath ) )
	{
		return ImportResult::Error( "Error parsing resource filename.");
	}

	String extension = red::utils::ExtractExtension( sourceFilePath );
	GpuWrapApi::eTextureImportFormat format = TexGroupHelpers::FileExtensionToFormat( extension );
	if ( !GpuWrapApi::ImportTexture( sourceFilePath, format, srcData, desc ) )
	{
		return ImportResult::Error( "Texture import failed" );
	}

	Uint32 pixelSize;
	ETextureRawFormat rawFormat;
	ETextureCompression texCompression;
	STextureGroupSetup texSetup;

	TexGroupHelpers::CreateBaseTextureGroup( desc.format, texSetup );
	TexGroupHelpers::MapGpuToEngineTextureFormat( desc.format, rawFormat, texCompression, pixelSize );

	RED_ASSERT( texSetup.m_rawFormat == rawFormat, "MapGpuToEngineTextureFormat AND CreateBaseTextureGroup returned mismatched data" );

	auto sourceTexture = CreateHandle< CSourceTexture >();
	sourceTexture->SetParent( resource.Get() );
	sourceTexture->Init( static_cast<Uint32>( desc.width ), static_cast<Uint32>( desc.height ), 1, rawFormat );

	ASSERT( srcData );

	// Allocate mipmap data
	sourceTexture->CreateFromRawData( srcData.Get(), static_cast<Uint32>( desc.width ), static_cast<Uint32>( desc.height ), 1, static_cast<Uint32>( desc.width * pixelSize ) );

	// Fill source data and mips
	ECubeSourceTextureType sourceType = CST_Invalid;
	Uint32 expectedHeightCrossH = static_cast<Uint32>( desc.width * 3 / 4 );
	Uint32 expectedHeightCrossW = static_cast<Uint32>( desc.width * 4 / 3 );
	Uint32 expectedHeightPanorama = static_cast<Uint32>( desc.width / 2 );
	if( desc.height == expectedHeightCrossH )
	{
		sourceType = CST_CrossHorizontal;
	}
	else if ( desc.height == expectedHeightCrossW )
	{
		sourceType = CST_CrossVertical;
	}
	else if( desc.height == expectedHeightPanorama )
	{
		sourceType = CST_Panorama;
	}
	else
	{
		return ImportResult::Error( "Invalid source data aspect ratio! Must be 4:3 or 2:1." );
	}
	
	resource->InitFromSourceData( sourceTexture, texSetup, sourceType );

	// Create all render blobs on import
	tools::CubeTextureCooker cooker( { *resource } );
	if ( !cooker.DoCook() )
	{
		return ImportResult::Error( "Texture import failed! - Error building render resource blob" );
	}

	return ImportResult::Success( resource );
}
