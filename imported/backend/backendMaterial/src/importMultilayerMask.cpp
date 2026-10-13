/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../backendEngine/include/importer.h"
#include "../../backendEngine/include/resourceMetadata.h"
#include "../../../common/renderData/include/imageProcessing.h"
#include "../../../common/renderData/include/renderSimpleTypes.h"
#include "../../../common/redReflection/include/util.h"
#include "../../../common/resourceMaterial/include/multilayerMask.h"
#include "multilayerMaskBuilder.h"
#include "multilayerMaskMetadata.h"

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/jsonFileHelper.h"
#include "../../../../external/rapidjson/include/rapidjson/error/en.h"


class MultilayerMaskFactory : public IFactory
{
	RTTI_DECLARE_TYPE( MultilayerMaskFactory );

public:
	MultilayerMaskFactory()
	{
		m_resourceClass = ClassID< Multilayer_Mask >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( MultilayerMaskFactory );
RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MultilayerMaskImporter : public IImporter
{
	RTTI_DECLARE_TYPE( MultilayerMaskImporter );

public:
	MultilayerMaskImporter();

	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options ) override;

private:
	struct Context
	{
		MultilayerMaskBuilder builder;
		ImportResult importResult;
		THandle< Multilayer_Mask > resource;
	};


	static Bool LoadSourceFromTiff( Context& ctx, const red::AbsolutePath& path );
	static Bool LoadSourceFromJson( Context& ctx, const red::AbsolutePath& path, red::DynArray<red::AbsolutePath>& outSourceImagePaths );
};


RTTI_BEGIN_TYPE( MultilayerMaskImporter )
	RTTI_PARENT_TYPE( IImporter )
RTTI_END_TYPE()


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MultilayerMaskImporter::MultilayerMaskImporter()
{
	m_resourceClass = ClassID< Multilayer_Mask >();

	m_formats.PushBack( CFileFormat( "tiff", "Tagged Image File Format" ) );
	m_formats.PushBack( CFileFormat( "tif", "Tagged Image File Format" ) );
	m_formats.PushBack( CFileFormat( "json", "JSON" ) );
}



Bool MultilayerMaskImporter::LoadSourceFromTiff( Context& ctx, const red::AbsolutePath& path )
{
	red::UniqueBuffer maskLayersData;
	GpuWrapApi::TextureDataDesc maskLayersDesc;

	if ( !GpuWrapApi::ImportTexture( path, GpuWrapApi::eTextureImportFormat::TIF_WIC, maskLayersData, maskLayersDesc, true, false, false ) )
	{
		ctx.importResult = ImportResult::Error( "Import: Failed to load image from %hs. %hs", path.AsChar(), GpuWrapApi::GetLastErrorString().AsChar() );
		return false;
	}

	red::DynArray<MultilayerMaskBuilder::LayerSource> layers{ red::PoolBackend() };

	layers.Resize( Uint32(maskLayersDesc.numSlices) );
	for ( Uint32 i = 0; i < maskLayersDesc.numSlices; ++i )
	{
		layers[i].imageData = red::OffsetPtr( maskLayersData.Get(), maskLayersDesc.slicePitch*i );
		layers[i].format = maskLayersDesc.format;
		layers[i].width = Uint32(maskLayersDesc.width);
		layers[i].height = Uint32(maskLayersDesc.height);
		layers[i].rowStride = Uint32(maskLayersDesc.rowPitch);

		// the mask contains a zero layer but that's always an implicit full white mask so we don't get bias for that
		Uint32 maskLayerIndex = i + 1;
		if (ctx.resource != nullptr && ctx.resource->GetLayerCount() > maskLayerIndex)
		{
			layers[i].bias = ctx.resource->GetLayerBias( maskLayerIndex, ECookingPlatform::PLATFORM_PC );
			layers[i].biasConsole = ctx.resource->GetLayerBias( maskLayerIndex, ECookingPlatform::PLATFORM_PS4 );
		}
	}

	ctx.builder.SetLayerSources( layers );

	if ( !ctx.builder.HasError() )
	{
		ctx.importResult = ImportResult::Error( "Import: %hs", ctx.builder.GetErrorString().AsChar() );
		return false;
	}

	return true;
}


Bool MultilayerMaskImporter::LoadSourceFromJson( Context& ctx, const red::AbsolutePath& path, red::DynArray<red::AbsolutePath>& outSourceImagePaths )
{
	using JSONDocument = rapidjson::JSONDocument;
	using JSONValue = rapidjson::JSONValue;

	JSONDocument doc;
	{
		String fileData;
		if ( !red::LoadFileToString( path, fileData ) )
		{
			ctx.importResult = ImportResult::Error("Import: Failed to read '%hs'", path.AsChar() );
			return false;
		}

		const Uint32 parseFlags = RAPIDJSON_NAMESPACE::kParseCommentsFlag | RAPIDJSON_NAMESPACE::kParseTrailingCommasFlag | RAPIDJSON_NAMESPACE::kParseNanAndInfFlag;
		doc.Parse<parseFlags>( fileData.AsChar() );
		if ( doc.HasParseError() )
		{
			ctx.importResult = ImportResult::Error("Import: Error parsing '%hs': %hs", path.AsChar(), RAPIDJSON_NAMESPACE::GetParseError_En(doc.GetParseError()) );
			return false;
		}
	}

	if ( !doc.HasMember("layers") || !doc["layers"].IsArray() )
	{
		ctx.importResult = ImportResult::Error("Import: Expected 'layers' array");
		return false;
	}

	const auto jLayersArray = doc["layers"].GetArray();
	const Uint32 numLayersToLoad = jLayersArray.Size();

	String containingPath = red::paths::ExtractParentPath(path);

	ctx.builder.ReserveLayers( numLayersToLoad + 1 );

	for ( Uint32 layer_i = 0; layer_i < numLayersToLoad; ++layer_i )
	{
		const JSONValue& jLayer = jLayersArray[layer_i];
		if ( !jLayer.IsObject() )
		{
			ctx.importResult = ImportResult::Error("Import: 'layers' should be array of objects");
			return false;
		}
		if ( !jLayer.HasMember("source") || !jLayer["source"].IsString() )
		{
			ctx.importResult = ImportResult::Error("Import: Element of 'layers' should have 'source' member with a String value");
			return false;
		}
		const JSONValue& jSource = jLayer["source"];
		const AnsiChar* sourceString = jSource.GetString();

		String sourcePath;
		if ( red::paths::IsAbsolutePath( sourceString ) )
		{
			sourcePath = sourceString;
		}
		else
		{
			sourcePath = containingPath + sourceString;
		}

		GpuWrapApi::eTextureImportFormat importFormat = GpuWrapApi::eTextureImportFormat::TIF_WIC;
		if ( red::paths::ExtractExtension(sourcePath).ToLower() == "tga" )
		{
			importFormat = GpuWrapApi::eTextureImportFormat::TIF_TGA;
		}

		red::AbsolutePath sourceAbsPath = red::AbsolutePath::CreateFilePath(sourcePath);
		outSourceImagePaths.PushBack(sourceAbsPath);

		red::UniqueBuffer maskLayerData;
		GpuWrapApi::TextureDataDesc maskLayerDesc;
		if ( !GpuWrapApi::ImportTexture( sourceAbsPath, importFormat, maskLayerData, maskLayerDesc, true, false, false ) )
		{
			ctx.importResult = ImportResult::Error( "Import: Failed to load image from %hs. %hs", sourcePath.AsChar(), GpuWrapApi::GetLastErrorString().AsChar() );
			return false;
		}

		if ( maskLayerDesc.numSlices != 1 )
		{
			ctx.importResult = ImportResult::Error( "Import: %hs: Expected single-layer image", sourcePath.AsChar() );
			return false;
		}

		MultilayerMaskBuilder::LayerSource layerSource;
		layerSource.imageData = maskLayerData.Get();
		layerSource.format = maskLayerDesc.format;
		layerSource.width = Uint32(maskLayerDesc.width);
		layerSource.height = Uint32(maskLayerDesc.height);
		layerSource.rowStride = Uint32(maskLayerDesc.rowPitch);

		// the mask contains a zero layer but that's always an implicit full white mask so we don't get bias for that
		Uint32 maskLayerIndex = layer_i + 1;
		if (ctx.resource != nullptr && ctx.resource->GetLayerCount() > maskLayerIndex)
		{
			layerSource.bias = ctx.resource->GetLayerBias( maskLayerIndex, ECookingPlatform::PLATFORM_PC );
			layerSource.biasConsole = ctx.resource->GetLayerBias( maskLayerIndex, ECookingPlatform::PLATFORM_PS4 );
		}

		ctx.builder.SetLayerSource(maskLayerIndex, layerSource );
	}

	if ( ctx.builder.HasError() )
	{
		ctx.importResult = ImportResult::Error( "Import: %hs", ctx.builder.GetErrorString().AsChar() );
		return false;
	}

	return true;
}


ImportResult MultilayerMaskImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options )
{
	THandle< Multilayer_Mask > resource = Cast< Multilayer_Mask >( inputResource );
	if( !resource )
	{
		return ImportResult::Error( "Import: Unable to import mask into something that is not a Multilayer_Mask" );
	}

	Context ctx;
	ctx.resource = resource;

	//
	// Load source mask images.
	//

	const red::AbsolutePath sourceFilePath = options->GetSourcePath();
	red::DynArray<red::AbsolutePath> sourceImages;

	{
		String extension = red::paths::ExtractExtension(sourceFilePath).ToLower();
		if ( extension == "tif" || extension == "tiff" )
		{
			sourceImages.PushBack(sourceFilePath);
			if ( !LoadSourceFromTiff( ctx, sourceFilePath ) )
			{
				return ctx.importResult;
			}
		}
		else if ( extension == "json" )
		{
			if ( !LoadSourceFromJson( ctx, sourceFilePath, sourceImages ) )
			{
				return ctx.importResult;
			}
		}
	}

	ctx.builder.Generate( resource );

	if ( !ctx.builder.HasError() )
	{
		THandle<Multilayer_MaskResourceImportMetadata> importMetadata( RED_NEW( Multilayer_MaskResourceImportMetadata ) );
		importMetadata->SetAssetPath( options->GetSourcePath() );
		importMetadata->SetSourceImagePaths( sourceImages );
		res::ResourceMetadata::SetImportData( resource, importMetadata );

		ctx.importResult = ImportResult::Success( resource );
	}
	else
	{
		ctx.importResult = ImportResult::Error( "Import: %hs", ctx.builder.GetErrorString().AsChar() );
	}

	return ctx.importResult;
}
