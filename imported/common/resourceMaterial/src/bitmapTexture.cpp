/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "bitmapTexture.h"
#include "sourceTextureData.h"

// The standalone AssetHost does not deploy the historical core/data-validation
// service. Its configuration disables validation, so the lightweight RED
// data-error macros preserve the no-op behavior without pulling that umbrella.
#if defined( VANGUARD_ASSET_HOST )
#include "../../redSystem/include/dataError.h"
#else
#include "../../core/include/dataError.h"
#endif

// Render data
#include "../../renderData/include/renderResource.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../renderData/include/renderer.h"
#endif

#include "../../redImage/include/image.h"
#include "../../redReflection/include/util.h"

#include "../../renderData/include/renderTextureBlob.h"
#include "../../redCore/include/globalModeInfo.h"

#include "../../redReflection/include/resourceMetricsBank.h"
#include "../../redReflection/include/variant.h"
#include "../../redReflection/include/gatheredResource.h"
#include "../../resource/include/2darray.h"

#ifndef NO_EDITOR
namespace hacks
{
static res::TStaticResource< C2dArray > s_downgradeExclusionList( RED_CONST_RESOURCEPATH( "engine\\editor\\downgrade_exclusion_list.csv" ), GRF_Startup | GRF_Backend );

bool CanTextureBeDowngraded( const res::ResourcePath& path )
{
	const Uint32 rowCount = s_downgradeExclusionList->GetNumberOfRows();
	for( Uint32 index = 1; index != rowCount; ++index )
	{
	 	const res::ResourcePath texturePath = res::ResourcePath::Build( s_downgradeExclusionList->GetValue( 0, index ).AsChar() );
		if( texturePath == path )
		{
			return false;
		}
	}

	auto view = path.ToStringView();
	if( view.StartsWith( "base\\environment\\decoration" ) )
	{
		return false;
	}

	return true;
}

void static ClonePCTextureToXboxAndPS4( rend::IRenderTextureBlob& pcTexture, THandle< rend::IRenderTextureBlob >& outXboxOneTexture, THandle< rend::IRenderTextureBlob >& outPS4Texture )
{
	const auto& pcBufferData = pcTexture.m_textureData.Editor_LoadContentDirect();

	if( outXboxOneTexture = rend::IRenderTextureBlob::CreateRenderTextureBlob( PLATFORM_XboxOne ) )
	{
		outXboxOneTexture->m_header = pcTexture.m_header;
		outXboxOneTexture->m_textureData.Editor_SetContentDirect( pcBufferData );
	}

	if( outPS4Texture = rend::IRenderTextureBlob::CreateRenderTextureBlob( PLATFORM_PS4 ) )
	{
		outPS4Texture->m_header = pcTexture.m_header;
		outPS4Texture->m_textureData.Editor_SetContentDirect( pcBufferData );
	}
}
}
#endif

using red::DynArray;
using red::String;

RTTI_BEGIN_TYPE( CBitmapTexture );
{
	RTTI_PARENT_TYPE( ITexture );
	RTTI_PROPERTY( m_width ).readOnly();
	RTTI_PROPERTY( m_height ).readOnly();
	RTTI_PROPERTY( m_depth ).readOnly();
	RTTI_PROPERTY( m_setup ).editable();
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_mipMapGenParams ).notCooked();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_histBiasMulCoef ).readOnly();
	RTTI_PROPERTY( m_histBiasAddCoef ).readOnly();
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_sourceData ).notCooked();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_renderResourceBlob );
	RTTI_PROPERTY( m_renderTextureResource );
}
RTTI_END_TYPE(); 

RTTI_BEGIN_TYPE( MipMapGenParams );
{
	RTTI_PROPERTY( m_applyToksvig_ShouldInvChannel ).readOnly();
	RTTI_PROPERTY( m_applyToksvig_Channel ).readOnly();
	RTTI_PROPERTY( m_applyToksvig_sourceNormalMap ).readOnly();
}
RTTI_END_TYPE();

CBitmapTexture::CBitmapTexture()
	: m_width( 0 )
	, m_height( 0 )
	, m_depth( 1 )
	, m_mipMapCount( 0 )
#ifndef RED_CONFIGURATION_FINAL
	, m_sourceData( NULL )
#endif // RED_CONFIGURATION_FINAL
	, m_histBiasMulCoef( Vector3(1.0f, 1.0f, 1.0f ) )
	, m_histBiasAddCoef( Vector3(0.0f, 0.0f, 0.0f ) )
	, m_renderResource( nullptr )
	, m_renderResourceBlobSize( 0 )
	, m_createRenderResourceFailed( false )
{
}

CBitmapTexture::~CBitmapTexture()
{
#ifdef USE_RESOURCE_METRICS_BANK
	if ( GResourceLoader && m_resourcePath.IsValid() )
	{
		if ( m_renderResource )
		{
			auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
			resourceMetricsBank->OnUnloadTexture( m_resourcePath );
		}
	}
#endif
}

Bool CBitmapTexture::OnPropertyMissing( CName propertyName, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "platformMipBiasXBOX" ) )
	{
		// Take from Xbox or PS4 - doesn't matter. Both have comparable memory and artists never differentiated.d
		readValue.Get<Uint8>( m_setup.m_platformMipBiasConsole );
		return true;
	}
	return TBaseClass::OnPropertyMissing( propertyName, readValue );
}

void CBitmapTexture::OnPreSave( const PreSaveContext& context )
{
	TBaseClass::OnPreSave( context );

#ifndef NO_EDITOR
	//////////////////////////////////////////////////////////////////////////
	// REMOVE AFTER RESAVE
	// No custom editor and converted from databuffer to DDB, so make sure will be autoloaded next time
	if ( auto oldBlob = Cast< rend::IRenderTextureBlob >( m_renderResourceBlob ) )
	{
		THandle< rend::IRenderTextureBlob > pcBlob{ rend::IRenderTextureBlob::CreateRenderTextureBlob( PLATFORM_PC ).ReleaseOwnership() };
		pcBlob->m_header = oldBlob->m_header;
		pcBlob->m_textureData.Editor_SetContentDirect( oldBlob->m_textureData.Editor_LoadContentDirect() );
				
		THandle< rend::IRenderTextureBlob > xboxBlob;
		THandle< rend::IRenderTextureBlob > ps4Blob;
		hacks::ClonePCTextureToXboxAndPS4( *pcBlob, xboxBlob, ps4Blob );
		InitRenderResourceBlob( PLATFORM_PC, pcBlob );
		InitRenderResourceBlob( PLATFORM_XboxOne, xboxBlob );
		InitRenderResourceBlob( PLATFORM_PS4, ps4Blob );
	}

	if( context.isCooking )
	{
		m_setup.m_allowTextureDowngrade = hacks::CanTextureBeDowngraded( GetPath() );
	}
#endif
}

void CBitmapTexture::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	RED_DATA_ERROR_BEGIN( red::LoggerCategory_Rendering, GetRenderResourceBlob(), "Bitmap Texture has no render blob. May need to reimport it." );
	RED_DATA_ERROR_ACTION_SHOW_ASSET( GetPath().ToDebugString() );
	RED_DATA_ERROR_END();

	if ( !context.flags.skipUploadRenderData )
	{
		(void)GetRenderResource();
	}
}

#ifndef NO_DATA_VALIDATION
void CBitmapTexture::OnCheckDataErrors() const
{
	// Pass to base class
	TBaseClass::OnCheckDataErrors();

	// Texture is empty
	if ( !m_width || !m_height )
	{
		DATA_HALT( DES_Major, this, ("Rendering"), ("Texture is empty and has no data") );
	}

	// Source data is empty
	if ( !m_sourceData && m_setup.m_hasMipchain && HasValidPath() )
	{
		DATA_HALT( DES_Tiny, this, ("Rendering"), ("Texture has no source data") );
	}

	// Texture is not assigned to any texture group
	const auto& resourceBlob = m_renderTextureResource.GetRenderResourceBlob();
	if ( ( m_setup.m_group == GpuWrapApi::TEXG_Unknown || !m_setup.m_group ) && resourceBlob )
	{
		const Uint32 currentMemory = static_cast< Uint32 >( resourceBlob->GetDataSize() );
		const Uint32 projectedMemory = currentMemory / 4;
		const Float wastedMemory = (Float)( currentMemory - projectedMemory ) * 1.33f / 1024.0f;
		if ( wastedMemory > 1024.0f )
		{
			DATA_HALT( DES_Minor, this, ("Rendering"), ("Texture has no texture group assigned. Around %1.2fMB of GPU memory is wasted."), wastedMemory / 1024.0f );
		}
		else
		{
			DATA_HALT( DES_Minor, this, ("Rendering"), ("Texture has no texture group assigned. Around %1.2fKB of GPU memory is wasted."), wastedMemory );
		}
	}

	// Failed to create rendering resource
	if ( !resourceBlob )
	{
		DATA_HALT( DES_Uber, this, ("Rendering"), ("Texture was not uploaded to GPU. Ask programmers.") );
	}
}
#endif // NO_DATA_VALIDATION

const ITexture::TResourcePtr CBitmapTexture::GetRenderResource() const
{
	ITexture::TResourcePtr renderResource;

	// try read-only lock first
	{
		RED_SCOPE_SHARED_LOCK( m_resourceCreationLock );
		renderResource = m_renderResource;
	}

	#if !defined( VANGUARD_ASSET_HOST )
	if ( !renderResource && GRender && !GRender->IsDeviceLost() )
	{
		RED_SCOPE_LOCK(m_resourceCreationLock);

		if (!m_renderResource)
		{
			// Recreate rendering resource
			CBitmapTexture* nonConstThis = const_cast<CBitmapTexture*>(this);
			nonConstThis->CreateRenderResource();
		}

		renderResource = m_renderResource;
	}
	#endif

	// Return created resource
	return renderResource;
}

void CBitmapTexture::CreateRenderResource()
{ 
#if defined( VANGUARD_ASSET_HOST )
	// AssetHost is a source/cook tool. Keep the authored render blob but never
	// instantiate a live GPU texture in this headless process.
	m_renderResource = nullptr;
#else
	auto newResource = m_renderResource;

	// Create driver version of texture
	const auto& resourceBlob = GetRenderResourceBlob();
	if( GRender && !GRender->IsDeviceLost() && resourceBlob )
	{
		

		m_resourcePath = GetPath();
		if ( !m_resourcePath.IsValid() )
		{
			if ( auto* parent = GetParent() )
			{
				m_resourcePath = parent->GetPath();
			}
		}

#ifdef USE_RESOURCE_METRICS_BANK
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
		if ( m_renderResource )
		{
			resourceMetricsBank->OnUnloadTexture( m_resourcePath );
		}
#endif

		m_renderResourceBlobSize = resourceBlob->GetDataSize();

		if( !m_createRenderResourceFailed )
		{
			// Create rendering resource
			newResource = GRender->UploadBlobTexture( SRenderDataSource{ GetPath(), m_renderResource }, resourceBlob, m_setup );

			if( !newResource )
			{
				m_createRenderResourceFailed = true;
			}

#ifdef USE_RESOURCE_METRICS_BANK
			if ( newResource && m_resourcePath.IsValid() )
			{
				res::SingleResourceMetrics memoryMetrics{};
				memoryMetrics.path = m_resourcePath;
				const Uint32 usedVideoMemory = newResource->GetUsedVideoMemory();
				memoryMetrics.gpuBytesUsed = usedVideoMemory > 0 ? usedVideoMemory : m_renderResourceBlobSize;
				memoryMetrics.texture.group = GetTextureGroup( GetTextureSetup().m_group );
				resourceMetricsBank->OnLoadTexture( memoryMetrics );
			}
#endif
		}
		else
		{
			newResource = nullptr;
		}
	}

	m_renderResource = newResource;
#endif
}

void CBitmapTexture::ReleaseRenderResource()
{
	RED_SCOPE_LOCK( m_resourceCreationLock );

#ifdef USE_RESOURCE_METRICS_BANK
	if( m_renderResource && GResourceLoader && m_resourcePath.IsValid() )
	{
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
		resourceMetricsBank->OnUnloadTexture( m_resourcePath );
	}
#endif
	
	m_renderResource = nullptr;
}

Bool CBitmapTexture::IsRenderResourceCreated() const
{
	return static_cast< Bool >( m_renderResource );
}

Uint32 CBitmapTexture::GetRenderResourceSize() const
{
	const auto& renderResource = GetRenderResourceBlob();
	return renderResource ? renderResource->GetDataSize() : m_renderResourceBlobSize;
}

const THandle< IRenderResourceBlob >& CBitmapTexture::GetRenderResourceBlob() const
{
	if ( const auto& blob = m_renderTextureResource.GetRenderResourceBlob() )
	{
		return blob;
	}

	// #todo: remove fallback after resave
	// If exists, DDB should already be loaded through databuffer -> DDB runtime conversion
	return m_renderResourceBlob;
}

void CBitmapTexture::GetAdditionalInfo( DynArray< String >& info ) const
{
	// Texture group
	{
		info.PushBack( String( GpuWrapApi::GetTextureGroupName( m_setup.m_group ) ) );
	}
}


Uint32 CBitmapTexture::GetGPUSize() const
{
	// Just return PC blob size for now.
	if ( m_renderTextureResource.GetRenderResourceBlob() )
	{
		return m_renderTextureResource.GetRenderResourceBlob()->GetDataSize();
	}
	else
	{
		return 0;
	}
}

#ifndef RED_CONFIGURATION_FINAL
Uint32 CBitmapTexture::GetGPUSize( ECookingPlatform platform ) const
{
	if ( const THandle< IRenderResourceBlob >& blob = m_renderTextureResource.GetCookingPlatfromResourceBlob( platform ) )
	{
		return blob->GetDataSize();
	}
	return 0;
}

img::ImagePtr CBitmapTexture::GetImage() const
{
	if( GetSourceData() != nullptr ) 
		return GetSourceData()->GetImage();

	return img::ImagePtr();
}
#endif // RED_CONFIGURATION_FINAL

void CBitmapTexture::EnableMipChain( Bool enable )
{
	m_setup.m_hasMipchain = enable;
}

Uint8 CBitmapTexture::GetMaxMipCount() const
{
	return CalcMipCount( m_width, m_height, m_depth, m_setup.m_hasMipchain );
}

Uint32 CBitmapTexture::GetDownscaleBiasForCookingPlatform( ECookingPlatform platform ) const 
{
	switch( platform )
	{
	case PLATFORM_PC:
	case PLATFORM_WindowsServer:return m_setup.m_platformMipBiasPC;
	case PLATFORM_PS4:		return m_setup.m_platformMipBiasConsole;
	case PLATFORM_XboxOne:	return m_setup.m_platformMipBiasConsole;
	}
	return 0;
}

void CBitmapTexture::SetDownscaleBiasForCookingPlatform( ECookingPlatform platform, Uint32 bias )
{
	switch ( platform )
	{
	case PLATFORM_PC:
	case PLATFORM_WindowsServer:
	{
		m_setup.m_platformMipBiasPC = bias;
		break;
	}
	case PLATFORM_PS4:
	case PLATFORM_XboxOne:
	{
		m_setup.m_platformMipBiasConsole = bias;
		break;
	}
	}
}

Uint32 CBitmapTexture::GetHighestMipForCookingPlatform( ECookingPlatform platform ) const
{
	Uint8 numMips = GetMaxMipCount();
	Uint8 firstMip = 0;

	if ( numMips > 1 )
	{
		// Based on platform, calculate number of mipmaps to drop
		Int32 downscaleBias = GetDownscaleBiasForCookingPlatform( platform );

		firstMip = ( Uint32 )Clamp< Int32 >( downscaleBias, 0, numMips - 1 );
	}

	return firstMip;
}

Uint32 CBitmapTexture::GetResidentMipForCookingPlatform( ECookingPlatform platform ) const
{
	const Uint32 highestMip = GetHighestMipForCookingPlatform( platform );
	const Uint32 numMips = GetMaxMipCount() - highestMip;

	// Determine first mipmap to use for texture
	Uint32 residentMipMap = 0;

	// up to 32x32 will be kept resident
	constexpr Uint32 maxResidentMips = 5;

	RED_STATIC_ASSERT( 32 == RED_FLAG( maxResidentMips ) );

	if ( m_setup.m_isStreamable && numMips > maxResidentMips )
	{
		residentMipMap = numMips - maxResidentMips;
	}

	return residentMipMap;
}


Bool CBitmapTexture::InitFromSourceData( const THandle< CSourceTexture >& sourceTexture, const STextureGroupSetup& setup, Bool silent /*= false*/, Bool recreateRenderResource )
{
#ifndef NO_TEXTURE_EDITING
	RED_ASSERT( sourceTexture->m_width > 0 && sourceTexture->m_height > 0 );
	RED_ASSERT( false == GpuWrapApi::IsTextureGroupSystem( setup.m_group ), "Trying to create texture using SYSTEM related texture group" );

	if ( !( sourceTexture->m_width > 0 && sourceTexture->m_height > 0 ) )
	{
		return false;
	}

	if ( !MarkModified() )
	{
		return false;
	}

	// Setup initial data
	m_setup			= setup;
	m_width			= sourceTexture->m_width;
	m_height		= sourceTexture->m_height;
	m_depth			= sourceTexture->m_depth;
	m_mipMapCount	= GetMaxMipCount();

	// Delete existing artist data
	if ( m_sourceData )
	{
		m_sourceData = NULL;
	}

	// Release old blob
	m_renderResourceBlob.Reset();
	m_renderTextureResource.Clear();

	m_sourceData = sourceTexture;
	m_sourceData->SetParent( this );

	// Cancel streaming
	#if !defined( VANGUARD_ASSET_HOST )
	if ( !silent && GRender )
	{
		// Send the cancel streaming command
		//GRender->GetRenderCommandHandler()->Add_CancelTextureStreaming();

		// Flush renderer
		GRender->Flush();
	}
	#endif

	if( recreateRenderResource )
	{
		// Upload to card
		{
			RED_SCOPE_LOCK( m_resourceCreationLock );
			CreateRenderResource();
		}

		// Recompile materials
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
		RecompileMaterials( this );
#endif
	}

	//CDrawableComponent::RecreateProxiesOfRenderableComponents();

#endif

	return true;
}

void CBitmapTexture::InitRenderResourceBlob( ECookingPlatform platform, const THandle< rend::IRenderTextureBlob >& blob )
{
	m_renderTextureResource.InitRenderResourceBlob( platform, blob );
	m_renderResourceBlobSize = blob->GetDataSize();
	m_renderResourceBlob.Reset();
	m_createRenderResourceFailed = false;
}

bool CBitmapTexture::Cooking_OptimiseRenderResourceBlob( ECookingPlatform platform )
{
	const auto savedBlob = Cast< rend::IRenderTextureBlob >( m_renderTextureResource.GetCookingPlatfromResourceBlob( platform ) );
	if ( savedBlob == nullptr )
	{
		RED_LOG_ERROR( "Cooking: Error getting platform specific render texture blob, cooked data will contain data for ALL PLATFORMS!" );
		return false;
	}

	m_renderTextureResource.Clear();
	m_renderTextureResource.InitRenderResourceBlob( platform, savedBlob );
	m_renderResourceBlobSize = savedBlob->GetDataSize();
	m_renderResourceBlob.Reset();
	m_createRenderResourceFailed = false;

	return true;
}
