/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../backend/backendEngine/include/importer.h"
#include "../../../common/resource/include/jsonFileHelper.h"
#include "../../../common/resourceMaterial/include/materialLayerLibrary.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/resourceMaterial/include/textureRegionSet.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/texture.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../backendEngine/include/backendEnginePublic.h"
#include "../../backendMesh/include/materialLayersLoader.h"

// Bitmaps
#include "../../../common/renderData/include/imageProcessing.h"
#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/resourceMaterial/include/textureArray.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/renderBackend/include/renderTextureCompiler.h"
#include "../../backendEngine/include/resourceBackend.h"
#include "../../backendMaterial/include/bitmapTextureCompilationSource.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/util.h"

using JSONDocument = rapidjson::JSONDocument;
using JSONValue = rapidjson::JSONValue;

class CTextureRegionSetImporter : public IImporter
{
	RTTI_DECLARE_TYPE( CTextureRegionSetImporter );

public:
	CTextureRegionSetImporter();

public:
	virtual ImportResult DoImport( THandle< CResource > resource, const THandle<ResourceImportOptions>& options ) override;

	template< typename T >
	TResRef< T > LoadResourceRef( const res::ResourcePath& resPath )
	{
		auto token = GResourceLoader->IssueLoadingRequest( resPath );
		token->WaitUntilLoaded();
		if( !token->GetResource() )
		{
			RED_LOG_ERROR( "Importer: Couldn't load just created texture region set: %s.", resPath.ToDebugString() );
		}

		return TResRef< T >( token->GetResource() );
	}
};

RTTI_BEGIN_TYPE( CTextureRegionSetImporter );
	RTTI_PARENT_TYPE( IImporter );
RTTI_END_TYPE();


CTextureRegionSetImporter::CTextureRegionSetImporter()
{
	// Supported class
	m_resourceClass = ClassID< CTextureRegionSet >();

	// Supported formats
	m_formats.PushBack( CFileFormat( "json", "Texture region set definition." ) );
}

Bool ParseRegionSets( const JSONValue& jsonRegionSets, red::DynArray< Vector4 >& regionSets )
{
	if ( !jsonRegionSets.IsArray() )
	{
		return false;
	}
	for ( auto it = jsonRegionSets.Begin(); it != jsonRegionSets.End(); ++it )
	{
		const JSONValue& jsonRegionSet = *it;
		if ( !jsonRegionSet.IsArray() )
		{
			return false;
		}
		Int32 counter = 0;
		Float values[ 4 ];
		for ( auto itVector = jsonRegionSet.Begin(); itVector != jsonRegionSet.End(); ++itVector )
		{
			if ( counter >= 4 )
			{
				return false;
			}
			values[ counter++ ] = (*itVector).GetFloat();
		}
		regionSets.PushBack( Vector4( values ) );
	}
	return true;
}

Bool ParseRegion( const JSONValue& jsonRegion, rend::TextureRegion& region )
{
	const JSONValue& name = jsonRegion[ "name" ];
	region.m_name = RED_NAME( name.GetString() );

	const JSONValue& isStretch = jsonRegion[ "isStretch" ];
	region.m_isStretch = isStretch.GetBool();

	const JSONValue& jsonRegionSets = jsonRegion[ "region_sets" ];
	if ( !jsonRegionSets.IsObject() )
	{
		return false;
	}

	red::DynArray< Vector4 > innerSets;
	red::DynArray< Vector4 > outerSets;
	{
		const JSONValue& jsonRegionSetsInner = jsonRegionSets[ "inner" ];
		if ( !ParseRegionSets( jsonRegionSetsInner, innerSets ) )
		{
			return false;
		}
		const JSONValue& jsonRegionSetsOuter = jsonRegionSets[ "outer" ];
		if ( !ParseRegionSets( jsonRegionSetsOuter, outerSets ) )
		{
			return false;
		}
	}
	if ( innerSets.Size() != outerSets.Size() )
	{
		return false;
	}

	for ( Uint32 i = 0; i < innerSets.Size(); ++i )
	{
		region.m_regionParts.PushBack( rend::TextureRegionPart( innerSets[ i ], outerSets[ i ] ) );
	}

	return true;
}

ImportResult CTextureRegionSetImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options )
{
	// Get the target object
	THandle< CTextureRegionSet > resource = Cast< CTextureRegionSet >( inputResource );
	if ( !resource )
	{
		return ImportResult::Error( "Engine: Unable to import texture region set." );
	}

	const red::AbsolutePath sourceFilePath = options->GetSourcePath();
	if( !red::utils::HasExtension( sourceFilePath ) )
	{
		return ImportResult::Error( "Error parsing resource filename." );
	}

	// load the file data
	String fileData {red::PoolBackend()};
	if ( !red::LoadFileToString( sourceFilePath, fileData ) )
	{
		return ImportResult::Error( "Couldn't load source json data." );
	}

	JSONDocument d;
	d.Parse< 0 >( fileData.AsChar() );
	if ( d.HasParseError() )
	{
		return ImportResult::Error( "Error parsing json data." );
	}

	const JSONValue& regions = d["regions"];
	if ( !regions.IsArray() )
	{
		return ImportResult::Error( "Invalid data in JSON file." );
	}
	for ( auto it = regions.Begin(); it != regions.End(); ++it )
	{
		const JSONValue& jsonRegion = ( *it );
		if ( !jsonRegion.IsObject() )
		{
			return ImportResult::Error( "Invalid data in JSON file." );
		}

		rend::TextureRegion region;
		if ( !ParseRegion( jsonRegion, region ) )
		{
			return ImportResult::Error( "Invalid data in JSON file." );
		}

		resource->AppendRegion( region );
	}

	return ImportResult::Success( resource );
}