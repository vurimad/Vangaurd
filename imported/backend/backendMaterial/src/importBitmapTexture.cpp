/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "bitmapTextureCooker.h"

#include "../../backendEngine/include/resourceMetadata.h"
#include "../../backendEngine/include/resourceImportMetadata.h"

#include "../../../backend/backendEngine/include/importer.h"
#include "../../../common/renderData/include/imageProcessing.h"
#if defined( VANGUARD_ASSET_HOST )
#include "../../../common/redImage/include/image.h"
#include "../../../common/redImage/include/imageAccess.h"
#include "../../../common/redImage/include/imageIterator.h"
#include "../../../common/redImage/include/imageUtils.h"
#endif
#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "importBitmapTexture.h"
#include "../../../common/redReflection/include/resourceDepot.h"

using red::DynArray;

RTTI_BEGIN_TYPE( TextureImportOptions );
	RTTI_PARENT_TYPE( ResourceImportOptions );
	RTTI_PROPERTY( m_textureGroup ).editable();
	RTTI_PROPERTY( m_textureCompressionType ).editable();
	RTTI_PROPERTY( m_premultiply ).editable();
	RTTI_PROPERTY( m_platformMipBiasPC ).editable().range( 0, MAX_MIP_BIAS_FOR_USER, true );
	RTTI_PROPERTY( m_platformMipBiasConsole ).editable().range( 0, MAX_MIP_BIAS_FOR_USER, true );
	RTTI_PROPERTY( m_overrideGammaSetting ).editable();
	RTTI_PROPERTY( m_isGamma ).editable();
RTTI_END_TYPE();

bool TextureImportOptions::ParseFromCommandLine( const red::CommandLine& stringOptions )
{
	String param;
	if( stringOptions.GetFirstParam( "group", param ) )
	{
		GpuWrapApi::eTextureGroup group = GpuWrapApi::GetTextureGroupByName( param.AsChar() );
		if( GpuWrapApi::TEXG_System_Begin < group && group < GpuWrapApi::TEXG_System_End )
		{
			RED_LOG_ERROR( "Cannot use system texture groups" );
			return false;
		}

		m_textureGroup = group;
	}

	if( stringOptions.GetFirstParam( "compressionType", param ) )
	{
		if( !GetCompressionTypeByName( param.AsChar(), m_textureCompressionType ) )
		{
			RED_LOG_ERROR( "Texture compression type parameter is incorrect." );
			return false;
		}
	}

	Int32 biasParam;
	if( stringOptions.GetFirstParam( "mipBiasPC", biasParam ) )
	{
		if( !IsMipBiasValid( biasParam ) )
		{
			RED_LOG_ERROR( "Downscale bias value for PC out of valid range" );
			return false;
		}
		m_platformMipBiasPC = static_cast< Uint8 >( biasParam );
	}

	if( stringOptions.GetFirstParam( "mipBiasConsole", biasParam ) )
	{
		if( !IsMipBiasValid( biasParam ) )
		{
			RED_LOG_ERROR( "Downscale bias value for PS4 out of valid range" );
			return false;
		}
		m_platformMipBiasConsole = static_cast< Uint8 >( biasParam );
	}

	m_premultiply = stringOptions.HasOption( "premultiply" );

	return true;
}

void TextureImportOptions::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
{
	if ( path.ToString().EndsWith( "overrideGammaSetting" ) )
	{
		NotifyPropertyChanged( rtti::AccessPath() );
	}
	TBaseClass::OnPropertyPostChange( path, oldValue, newValue );
}

void TextureImportOptions::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
{
	outRootProperties.RemoveIf( [this]( const rtti::ClassEditablePropertyInfo& info )
	{
		if ( info.m_name == RED_NAME_CONSTEXPR_NOREG( "isGamma" ) )
		{
			return !m_overrideGammaSetting;
		}
		return false;
	} );
}

Bool TextureImportOptions::IsMipBiasValid( Int32 mipBias )
{
	return ( ( mipBias >= 0 ) && ( mipBias <= MAX_MIP_BIAS_FOR_USER ) );
}

Bool TextureImportOptions::GetTextureRawFormatByName( const AnsiChar* textureRawFormatName, ETextureRawFormat& textureRawFormat )
{
	textureRawFormat = TRF_Max;

	if( const rtti::EnumType* enumDesc = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR_NOREG( "ETextureRawFormat" ) ) )
	{
		Int64 value = -1;
		String valueText( "TRF_" );
		valueText += textureRawFormatName;
		if( enumDesc->FindValue( RED_NAME_NOREG( valueText ), value ) )
		{
			textureRawFormat = static_cast< ETextureRawFormat >( value );
		}
	}

	return textureRawFormat != TRF_Max;
}


Bool TextureImportOptions::NeedOptions() const
{
	return true;
}

Bool TextureImportOptions::GetCompressionTypeByName( const AnsiChar* compressionTypeName, ETextureCompression& textureCompressionType )
	{
		static_assert( TCM_Max == 15, "Add missing raw format type" );

		textureCompressionType = TCM_Max;

		if( const rtti::EnumType* enumDesc = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR_NOREG( "ETextureCompression" ) ) )
		{
			Int64 value = -1;
			String valueText( "TCM_" );
			valueText += compressionTypeName;
			if( enumDesc->FindValue( RED_NAME_NOREG( valueText ), value ) )
			{
				textureCompressionType = static_cast< ETextureCompression >( value );
			}
		}

		return textureCompressionType != TCM_Max;
	}

void TextureImportOptions::OnResourcePathSet()
{
	if ( !GetResourceDepot()->ResourceExists( GetResourcePath() ) )
	{
		return;
	}

	TResRef< CBitmapTexture > existingResourceRef( GetResourcePath() );
	if ( THandle< CBitmapTexture > existingResource = existingResourceRef.Get() )
	{
		const STextureGroupSetup& setup = existingResource->GetTextureSetup();
		m_textureGroup = setup.m_group;
		m_textureCompressionType = setup.m_compression;
		m_platformMipBiasPC = setup.m_platformMipBiasPC;
		m_platformMipBiasConsole = setup.m_platformMipBiasConsole;
	}
}

/// Bitmap texture importer based on DirectXTex functionality
class CBitmapImporter : public IImporter
{
	RTTI_DECLARE_TYPE( CBitmapImporter );

public:
	GpuWrapApi::eTextureGroup m_textureGroup;
	ETextureCompression m_textureCompressionType;

public:
	CBitmapImporter();

public:
	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options ) override;
	virtual THandle<ResourceImportOptions> CreateOptions( const red::AbsolutePath& sourcePath ) override;

private:
	GpuWrapApi::eTextureGroup ExtractTextureGroupFromStemSuffix(const String& fileStem);
	Bool PrepareForImport( const String& fileStem, THandle< TextureImportOptions >& textureOptions );
};

RTTI_BEGIN_TYPE( CBitmapImporter )
RTTI_PARENT_TYPE( IImporter )
RTTI_END_TYPE()



CBitmapImporter::CBitmapImporter()
{
	// Supported class
	m_resourceClass			= ClassID< CBitmapTexture >();
	m_textureGroup			= GpuWrapApi::TEXG_Generic_Color;

	// Supported formats
	m_formats.PushBack( CFileFormat( "dds", "DirectDraw Surface" ) );
	m_formats.PushBack( CFileFormat( "bmp", "Windows Bitmap" ) );
	m_formats.PushBack( CFileFormat( "jpg", "Joint Photographics Experts Group" ) );
	m_formats.PushBack( CFileFormat( "tga", "Truevision Targa" ) );
	m_formats.PushBack( CFileFormat( "png", "Portable Network Graphics" ) );
	m_formats.PushBack( CFileFormat( "tiff", "Tagged Image File Format" ) );
	m_formats.PushBack( CFileFormat( "tif", "Tagged Image File Format" ) );
	m_formats.PushBack( CFileFormat( "exr", "EXR" ) );
	m_formats.PushBack( CFileFormat( "cube", "Cube LUT" ) );
}

THandle<ResourceImportOptions> CBitmapImporter::CreateOptions( const red::AbsolutePath& sourcePath )
{
	THandle< TextureImportOptions > options = CreateHandle<TextureImportOptions>();
	options->m_textureGroup = ExtractTextureGroupFromStemSuffix( red::utils::ExtractFileStem( sourcePath ) );

	STextureGroupSetup texSetup;
	TexGroupHelpers::CreateBaseTextureGroup( options->m_textureGroup, texSetup );
	options->m_textureCompressionType = texSetup.m_compression;
	return options;
}

GpuWrapApi::eTextureGroup CBitmapImporter::ExtractTextureGroupFromStemSuffix(const String& fileStem)
{
	GpuWrapApi::eTextureGroup ret = GpuWrapApi::TEXG_Generic_Color;

	if (fileStem.EndsWith(("_n")))
	{
		ret = GpuWrapApi::TEXG_Generic_Normal;
	}
	else if (fileStem.EndsWith(("_rm")))
	{
		ret = GpuWrapApi::TEXG_Generic_Color;
	}
	else if (fileStem.EndsWith(("_r")))
	{
		ret = GpuWrapApi::TEXG_Generic_Grayscale;
	}
	else if (fileStem.EndsWith(("_m")))
	{
		ret = GpuWrapApi::TEXG_Generic_Grayscale;
	}
	else if (fileStem.EndsWith(("_b")))
	{
		ret = GpuWrapApi::TEXG_Generic_Color;
	}
	else if (fileStem.EndsWith(("nm")))
	{
		ret = GpuWrapApi::TEXG_Generic_Normal;
	}
	else if (fileStem.EndsWith(("_data")))
	{
		ret = GpuWrapApi::TEXG_Generic_Data;
	}
	else if (fileStem.EndsWith(("_lut")))
	{
		ret = GpuWrapApi::TEXG_Generic_LUT;
	}
	else
	{
		RED_LOG_WARNING("Unknown texture group for given suffix");
	}

	return ret;
}

Bool CBitmapImporter::PrepareForImport( const String& fileStem, THandle< TextureImportOptions >& textureOptions )
{
	if ( textureOptions )
	{
		m_textureGroup = textureOptions->m_textureGroup;
		m_textureCompressionType = textureOptions->m_textureCompressionType;
	}
	else
	{
		m_textureGroup = ExtractTextureGroupFromStemSuffix( fileStem );
	}

	return true;
}

ImportResult CBitmapImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options )
{
	// Get the target object
	THandle< CBitmapTexture > resource = Cast< CBitmapTexture >( inputResource );
	if( !resource )
	{
		return ImportResult::Error( "Core: Unable to import bitmap into something that is not a bitmap" );
	}

	GpuWrapApi::TextureDataDesc desc;

	const red::AbsolutePath sourceFilePath = options->GetSourcePath();
	if( !red::utils::HasExtension( sourceFilePath ) )
	{
		return ImportResult::Error( "Error parsing resource filename." );
	}

	// Assign texture group
	THandle< TextureImportOptions > textureOptions = Cast< TextureImportOptions >( options );
	RED_FATAL_ASSERT( textureOptions != nullptr );

	// TODO refactor this whole thing, all the functions 
	// overwrite textureOptions and m_textureGroup in so unpredictable way

	PrepareForImport( red::utils::ExtractFileStem( sourceFilePath ), textureOptions );
	Bool shouldBePowerOf2 = m_textureGroup != GpuWrapApi::TEXG_Generic_Data;

	String extension = red::utils::ExtractExtension( sourceFilePath );
	GpuWrapApi::eTextureImportFormat format = TexGroupHelpers::FileExtensionToFormat( extension );
	red::UniqueBuffer srcData;
	if( !GpuWrapApi::ImportTexture( sourceFilePath, format, srcData, desc, shouldBePowerOf2, m_textureGroup == GpuWrapApi::TEXG_Generic_Data ) )
	{
		const auto erroLog = GpuWrapApi::GetLastErrorString();
		return ImportResult::Error( "Texture import failed! - Error importing texture. Error: %s", erroLog.AsChar() );
	}

	// if size is invalid for renderer, mark texture as data
	if ( m_textureGroup != GpuWrapApi::TEXG_Generic_Data && m_textureGroup != GpuWrapApi::TEXG_Generic_Font && ( desc.width % 4 || desc.height % 4 ) )
	{
		m_textureGroup = GpuWrapApi::TEXG_Generic_Data;
	}

	if ( format == GpuWrapApi::TIF_CUBE )
	{
		m_textureGroup = GpuWrapApi::TEXG_Generic_LUT;
		m_textureCompressionType = TCM_None;
	}

	STextureGroupSetup texSetup;
	// sets the:
	// m_rawFormat
	// m_compression
	// m_hasMipchain
	// m_isStreamable
	TexGroupHelpers::CreateBaseTextureGroup( m_textureGroup, texSetup );
	texSetup.m_isGamma = m_textureGroup == GpuWrapApi::TEXG_Generic_Color || m_textureGroup == GpuWrapApi::TEXG_Multilayer_Color || m_textureGroup == GpuWrapApi::TEXG_Generic_UI;

	if ( textureOptions->m_overrideGammaSetting )
	{
		texSetup.m_isGamma = textureOptions->m_isGamma;
	}

	Bool isWidthPowerOf2 = red::IsPowerOf2( static_cast< Int32 >( desc.width ) );
	Bool isHeightPowerOf2 = red::IsPowerOf2( static_cast< Int32 >( desc.height ) );
	if ( ( !isWidthPowerOf2 || !isHeightPowerOf2 ) && shouldBePowerOf2 )
	{
		RED_LOG_WARNING( "Imported texture '%hs' has size [%dx%d] which is not power of 2. It will have no mipmap chain.", sourceFilePath.AsChar(), desc.width, desc.height );
		texSetup.m_hasMipchain = false;	
	}

	Uint32 pixelSize;
	// sets the:
	// outRawFormat
	// outCompression - already set!
	// outPixelSize
	ETextureCompression keepThisCompression = texSetup.m_compression;
	TexGroupHelpers::MapGpuToEngineTextureFormat( desc.format, texSetup.m_rawFormat, texSetup.m_compression, pixelSize );
	texSetup.m_compression = m_textureGroup == GpuWrapApi::TEXG_Generic_Data ? TCM_None : keepThisCompression;

	// override compression type by custom value from import options if it is defined
	if( m_textureCompressionType != TCM_None )
	{
		texSetup.m_compression = m_textureCompressionType;
	}

	auto sourceTexture = CreateHandle< CSourceTexture >();
	sourceTexture->SetParent( resource.Get() );

	Uint32 depth = static_cast<Uint32>( desc.depth );
	Uint32 width = static_cast<Uint32>( desc.width );
	Uint32 height = static_cast<Uint32>( desc.height );
	sourceTexture->Init( width, height, depth, texSetup.m_rawFormat );

	// Allocate mipmap data
	RED_ASSERT( srcData );
	sourceTexture->CreateFromRawData( srcData.Get(), width, height, depth, width * pixelSize );

	// Sets mip bias per platform
	// Smallest usable mip is 4x4 pixels due to DXT compression (see renderBackend\src\renderTextureCompilerUtils.h)
	const Int32 numMips = ITexture::CalcMipCount( width, height, depth, texSetup.m_hasMipchain );
	const Int32 mipBiasMaxUnclamped = numMips - 3;
	const Uint8 mipBiasMax = static_cast< Uint8 >( ::Max( 0, mipBiasMaxUnclamped ) );

	texSetup.m_platformMipBiasPC = ::Clamp< Uint8 >( textureOptions->m_platformMipBiasPC, 0, mipBiasMax );
	texSetup.m_platformMipBiasConsole = ::Clamp< Uint8 >( textureOptions->m_platformMipBiasConsole, 0, mipBiasMax );

	// Premultiply
	if ( textureOptions->m_premultiply )
	{
		img::ImagePtr sourceImage = sourceTexture->GetImage();
		img::ImageAccess src( *sourceImage );
		img::ImageIterator it = sourceImage->GetIterator();

		auto func = []( img::ImageIterator& it, img::ImagePixel& pixel )
		{
			Float alpha = pixel[3];
			pixel.MulInplace( alpha );
			pixel[3] = alpha;
			return true;
		};
		img::utils::ProcessImageInplace( src, it, func );

		sourceTexture->SetImage( sourceImage );
	}

	// Fill source data and mips
	resource->InitFromSourceData( sourceTexture, texSetup, true, false );

	// Create all render blobs on import
	tools::BitmapTextureCookerSetup cookerSetup( *resource );
	cookerSetup.cookPC = true;
	cookerSetup.cookDURANGO = true;
	cookerSetup.cookORBIS = true;
	cookerSetup.cookWINDOWS_SERVER = true;
	tools::BitmapTextureCooker cooker( cookerSetup );
	if ( !cooker.DoCook() )
	{
		return ImportResult::Error( "Texture import failed! - Error building render resource blob" );
	}

	THandle<res::ResourceImportMetadata> importMetadata = CreateHandle<res::ResourceImportMetadata>();
	importMetadata.Get()->SetAssetPath( sourceFilePath );
	res::ResourceMetadata::SetImportData( resource, importMetadata );

#if 1
	if( resource->IsRenderResourceCreated() )
	{
		resource->CreateRenderResource();

		// Recompile materials
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
		RecompileMaterials( resource.Get() );
#endif
	}
#endif

	return ImportResult::Success( resource );
}
