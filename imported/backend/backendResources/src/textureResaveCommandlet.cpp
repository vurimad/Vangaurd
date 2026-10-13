/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resaveCommandlet.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../../common/redImage/include/imageAccess.h"
#include "../../backendMaterial/include/bitmapTextureCompilationSource.h"
#include "../../../common/redReflection/include/util.h"
#include "../../../common/renderBackend/include/renderTextureCompiler.h"
#include "../../backendMaterial/include/bitmapTextureCooker.h"
#include "../../../common/resourceMaterial/include/textureArray.h"
#include "../../../common/resourceMaterial/include/cubeTexture.h"
#include "../../backendMaterial/include/cubeTextureCooker.h"
#include "../../backendMaterial/include/textureArrayCooker.h"

class TextureResaveCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( TextureResaveCommandlet );

public:

	TextureResaveCommandlet();
	virtual ~TextureResaveCommandlet();

	virtual const char* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;

private:

	virtual bool OnResourcePreSave( CResource & resource ) override final;
	bool ProcessBitmapTexture( CBitmapTexture & texture );
	bool ProcessCubeTexture( CCubeTexture& texture );
	bool ProcessTextureArray( CTextureArray& textureArray );
};

RTTI_BEGIN_TYPE( TextureResaveCommandlet )
	RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

TextureResaveCommandlet::TextureResaveCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "textureResave" ) )
{}

TextureResaveCommandlet::~TextureResaveCommandlet()
{}

const char* TextureResaveCommandlet::GetOneLiner() const
{
	return nullptr;	
}

void TextureResaveCommandlet::PrintHelp() const 
{
	
}

bool TextureResaveCommandlet::OnResourcePreSave( CResource & resource )
{
	// Shouldn't matter if texturearray resaved before bitmap texture, since all have independent render resource blobs
	if( resource.IsA< CBitmapTexture >() )
	{
		return ProcessBitmapTexture( static_cast< CBitmapTexture& >( resource ) );
	}
	else if ( resource.IsA< CCubeTexture >() )
	{
		return ProcessCubeTexture( static_cast< CCubeTexture& >( resource ) );
	}
	else if ( resource.IsA< CTextureArray >() )
	{
		return ProcessTextureArray( static_cast< CTextureArray& >( resource ) );
	}
	
	RED_LOG_ERROR( "Resource %hs cannot be processed by textureResave commandlet", resource.GetPath().ToDebugString() );
	return false;
}

bool TextureResaveCommandlet::ProcessBitmapTexture( CBitmapTexture & texture )
{	
	tools::BitmapTextureCooker cooker( texture );
	if ( cooker.DoCook() )
	{
		return true;
	}
	
	RED_LOG_ERROR( "Failed to execute bitmap texture resave on %hs", texture.GetPath().ToDebugString() );
	return false;
}

bool TextureResaveCommandlet::ProcessCubeTexture( CCubeTexture& cubeTexture )
{
	tools::CubeTextureCooker cooker( { cubeTexture } );
	if ( cooker.DoCook() )
	{
		return true;
	}

	RED_LOG_ERROR( "Failed to execute cube texture resave on %hs", cubeTexture.GetPath().ToDebugString() );
	return false;
}

bool TextureResaveCommandlet::ProcessTextureArray( CTextureArray& textureArray )
{
	tools::TextureArrayCooker cooker( { textureArray } );
	if ( cooker.DoCook() )
	{
		return true;
	}

	RED_LOG_ERROR( "Failed to execute texture array resave on %hs", textureArray.GetPath().ToDebugString() );
	return false;
}
