/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendData/include/cooker.h"
#include "../../backendData/include/dataTypesImpl.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../../common/resourceMaterial/include/textureArray.h"
#include "../../../common/resourceMaterial/include/cubeTexture.h"
#include "../../../common/resourceMaterial/include/multilayerMask.h"
#include "../../../common/renderData/include/envProbeResource.h"
#endif

//-----------------------------------------------------------------------------

class RealTextureCooker : public cooker::BasicCooker
{
	RTTI_DECLARE_TYPE( RealTextureCooker );
public:
	virtual ~RealTextureCooker() override;

	virtual Uint32 GetVersion() const override;
	virtual bool CanCook( const res::ResourcePath& resourcePath ) const override;
	virtual bool CookLoadedResource( THandle< CResource >& resource, data::Context& context ) const override;
};

RTTI_BEGIN_TYPE( RealTextureCooker );
RTTI_PARENT_TYPE( cooker::BasicCooker );
RTTI_END_TYPE();

RealTextureCooker::~RealTextureCooker() = default;

Uint32 RealTextureCooker::GetVersion() const
{
	return 8;
}

bool RealTextureCooker::CanCook( const res::ResourcePath& resourcePath ) const
{
	auto extension = red::paths::GetExtension( resourcePath.ToStringView() );
	const bool isBitmap = ( extension == CBitmapTexture::GetDeprecatedFileExtension() || extension == CBitmapTexture::GetFileExtension() );
#if defined( VANGUARD_ASSET_HOST )
	return isBitmap;
#else
	const bool isArray = ( extension == CTextureArray::GetDeprecatedFileExtension() || extension == CTextureArray::GetFileExtension() );
	const bool isCube = ( extension == CCubeTexture::GetDeprecatedFileExtension() || extension == CCubeTexture::GetFileExtension() );
	const bool isMlMask = ( extension == Multilayer_Mask::GetDeprecatedFileExtension() || extension == Multilayer_Mask::GetFileExtension() );
	const bool isEnvProbe = ( extension == CReflectionProbeDataResource::GetDeprecatedFileExtension() || extension == CReflectionProbeDataResource::GetFileExtension() );
	return isBitmap || isArray || isCube || isMlMask || isEnvProbe;
#endif
}

bool RealTextureCooker::CookLoadedResource( THandle< CResource >& resource, data::Context& context ) const
{
	bool result = false;

	auto cookingPlatform = data::CookingPlatformFromTargetPlatform( context.TargetPlatform() );
	if ( THandle< CBitmapTexture > texture = Cast< CBitmapTexture >( resource ) )
	{
		result = texture->Cooking_OptimiseRenderResourceBlob( cookingPlatform );
	}
#if !defined( VANGUARD_ASSET_HOST )
	else if ( THandle< CTextureArray > texture = Cast< CTextureArray >( resource ) )
	{
		result = texture->Cooking_OptimiseRenderResourceBlob( cookingPlatform );
	}
	else if ( THandle< CCubeTexture > texture = Cast< CCubeTexture >( resource ) )
	{
		result = texture->Cooking_OptimiseRenderResourceBlob( cookingPlatform );
	}
	else if ( THandle< Multilayer_Mask > mlmask = Cast< Multilayer_Mask >( resource ) )
	{
		result = mlmask->Cooking_OptimiseRenderResourceBlob( cookingPlatform );
	}
	else if ( THandle< CReflectionProbeDataResource > mlmask = Cast< CReflectionProbeDataResource >( resource ) )
	{
		result = mlmask->Cooking_OptimiseRenderResourceBlob( cookingPlatform );
	}
#endif
	else
	{
		context.ErrorHandler().ReportError( "Unknown texture resource" );
	}

	if ( !result )
	{
		context.ErrorHandler().ReportError( "Failed to optimise the texture resource" );
	}

	return result;
}

//-----------------------------------------------------------------------------

