/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "cubeTexture.h"
#include "sourceTextureData.h"

// Core
#include "../../redReflection/include/serializationLoader.h"
#include "../../core/include/dataError.h"

// Redner data
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderer.h"
#include "../../redImage/include/imageUtils.h"
#include "../../redImage/include/image.h"
#include "../../redReflection/include/util.h"
#include "../../renderData/include/renderTextureBlob.h"
#include "../../redCore/include/globalModeInfo.h"

#include "../../redReflection/include/resourceMetricsBank.h"

#ifndef NO_EDITOR
namespace hacks
{
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

RTTI_BEGIN_TYPE( CCubeTexture );
{
	RTTI_PARENT_TYPE( ITexture );
	RTTI_PROPERTY( m_setup );
	RTTI_PROPERTY( m_size ).notCooked().readOnly();
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_targetFaceSize ).notCooked().editable();
	RTTI_PROPERTY( m_sourceTextureType ).notCooked();
	RTTI_PROPERTY( m_sourceData ).notCooked();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_renderResourceBlob );
	RTTI_PROPERTY( m_renderTextureResource );
}
RTTI_END_TYPE();

RTTI_BEGIN_ENUM( ECubeSourceTextureType );
{
	RTTI_ENUM_OPTION( CST_CrossHorizontal );
	RTTI_ENUM_OPTION( CST_CrossVertical );
	RTTI_ENUM_OPTION( CST_Panorama );
}
RTTI_END_ENUM();

CCubeTexture::CCubeTexture()
	: m_renderResource( nullptr )
#ifndef RED_CONFIGURATION_FINAL
	, m_sourceData( nullptr )
	, m_targetFaceSize( 0 )
#endif // RED_CONFIGURATION_FINAL
	, m_renderResourceBlobSize( 0 )
	, m_createRenderResourceFailed( false )
{
}

CCubeTexture::~CCubeTexture()
{
#ifdef USE_RESOURCE_METRICS_BANK
	if ( GResourceLoader && m_resourcePath.IsValid() && m_renderResource )
	{
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
		resourceMetricsBank->OnUnloadTexture( m_resourcePath );
	}
#endif
}

void CCubeTexture::OnPropertyPostChange( const rtti::Property* property )
{
	// OnPropertyPostChange( const rtti::Property* ) is not used anymore
	// if you want to ressurect this code, please override proper ISerializable::OnPropertyPostChange
	// and correct the code above to work with that

	(void)GetRenderResource();
}

void CCubeTexture::OnSerialize( IFile& file )
{
	TBaseClass::OnSerialize( file );

	if ( file.IsMapper() )
	{
		return;
	}
}

Bool CCubeTexture::InitFromSourceData( const THandle< CSourceTexture >& sourceTexture, const STextureGroupSetup& setup, ECubeSourceTextureType sourceType )
{
#ifndef NO_TEXTURE_EDITING
	ASSERT( sourceTexture->m_width > 0 && sourceTexture->m_height > 0 );

	if ( !( sourceTexture->m_width > 0 && sourceTexture->m_height > 0 ) )
	{
		return false;
	}

	if ( !MarkModified() )
	{
		return false;
	}

	// Setup initial data
	switch( sourceType )
	{
	case CST_CrossHorizontal:
		{
			Uint32 width = sourceTexture->m_width / 4;
			Uint32 height = sourceTexture->m_height / 3;
			RED_ASSERT( width == height, "Cross source data should be in 4:3 ratio!" );
			if( width != height )
				return false;
		
			m_size = width;
		}
		break;
	case CST_CrossVertical:
		{
			Uint32 width = sourceTexture->m_width / 3;
			Uint32 height = sourceTexture->m_height / 4;
			RED_ASSERT( width == height, "Cross source data should be in 4:3 ratio!" );
			if( width != height )
				return false;

			m_size = width;
		}
		break;
	case CST_Panorama:
		{
			Uint32 width = sourceTexture->m_width;
			Uint32 height = sourceTexture->m_height * 2;
			RED_ASSERT( width == height, "Panorama source data should be in 2:1 ratio!" );
			if( width != height )
				return false;

			m_size = width / 4;
		}
		break;
	}
	
	m_setup				= setup;
	m_sourceTextureType = sourceType;

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

	// Upload to card
	{
		RED_SCOPE_LOCK( m_resourceCreationLock );
		CreateRenderResource();
	}

	// Recompile materials
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	RecompileMaterials( this );
#endif

#endif

	return true;
}

void CCubeTexture::InitRenderResourceBlob( ECookingPlatform platform, const THandle< rend::IRenderTextureBlob >& blob )
{
	m_renderTextureResource.InitRenderResourceBlob( platform, blob );
	m_renderResourceBlobSize = blob->GetDataSize();
	m_renderResourceBlob.Reset();
}

bool CCubeTexture::Cooking_OptimiseRenderResourceBlob( ECookingPlatform platform )
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

	return true;
}

void CCubeTexture::OnPreSave( const PreSaveContext& context )
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
#endif
}

const THandle< IRenderResourceBlob >& CCubeTexture::GetRenderResourceBlob() const
{
	if ( m_renderTextureResource.GetRenderResourceBlob() )
	{
		return m_renderTextureResource.GetRenderResourceBlob();
	}

	// #todo: remove fallback after resave
	return m_renderResourceBlob;
}

void CCubeTexture::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	if ( !context.flags.skipUploadRenderData )
	{
		// Will almost certainly crash soon anyway
		RED_ERROR( GetRenderResourceBlob(), "Render resource blob is not available for cube texture: %hs.", GetFriendlyName().AsChar() );

		(void)GetRenderResource();
	}
}

Uint32 CCubeTexture::GetRenderResourceSize() const
{
	if ( m_renderResourceBlobSize != 0 )
	{
		return m_renderResourceBlobSize;
	}
	else if ( const auto& renderResource = GetRenderResourceBlob() )
	{
		return renderResource->GetDataSize();
	}

	return 0;
}

void CCubeTexture::CreateRenderResource()
{
	auto newResource = m_renderResource;

	const auto& resourceBlob = GetRenderResourceBlob();
	if ( GRender && !GRender->IsDeviceLost() && resourceBlob )
	{
		

		m_resourcePath = GetPath();
		if ( !m_resourcePath.IsValid() )
		{
			if ( auto* parent = GetParent() )
			{
				m_resourcePath = parent->GetPath();
			}

			RED_ASSERT( m_resourcePath.IsValid() );
		}

#ifdef USE_RESOURCE_METRICS_BANK
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
		if ( m_renderResource )
		{
			resourceMetricsBank->OnUnloadTexture( m_resourcePath );
		}
#endif

		m_renderResourceBlobSize = resourceBlob->GetDataSize();

		if(!m_createRenderResourceFailed)
		{
			// Create rendering resource
			newResource = GRender->UploadBlobTexture( SRenderDataSource{ GetPath(), newResource }, resourceBlob, m_setup ) ;

			if(!newResource && IsGameMode())
			{
				m_createRenderResourceFailed = true;
			}

#ifdef USE_RESOURCE_METRICS_BANK
			if ( newResource )
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
}

void CCubeTexture::ReleaseRenderResource()
{
	RED_SCOPE_LOCK( m_resourceCreationLock );

#ifdef USE_RESOURCE_METRICS_BANK
	if( m_renderResource && GResourceLoader && m_resourcePath.IsValid() )
	{
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
		resourceMetricsBank->OnUnloadTexture( m_resourcePath );
	}
#endif

	// Release previous cube
	m_renderResource = nullptr;
}

const ITexture::TResourcePtr CCubeTexture::GetRenderResource() const
{
	RED_SCOPE_LOCK( m_resourceCreationLock );
	if ( !m_renderResource && GRender && !GRender->IsDeviceLost() )
	{
		const_cast<CCubeTexture*>( this )->CreateRenderResource();
	}

	return m_renderResource;
}

#ifndef RED_CONFIGURATION_FINAL
img::ImagePtr CCubeTexture::GetImage() const
{
	if( GetSourceData() != nullptr ) 
		return GetSourceData()->GetImage();

	return img::ImagePtr();
}

img::ImagePtr CCubeTexture::GetImage( Uint16 texSlice ) const
{
	if( GetSourceData() != nullptr ) 
	{
		if( m_sourceTextureType == CST_CrossHorizontal )
		{
			Uint16 x = 0, y = 0;
			switch( texSlice )
			{
			case 0: x = 2 * m_size;  y = m_size;		break; // +x
			case 1: x = 0;			 y = m_size;		break; // -x
			case 2: x = m_size;		 y = m_size;		break; // +z
			case 3: x = 3 * m_size;  y = m_size;		break; // -z
			case 4: x = m_size;		 y = 0;				break; // +y
			case 5: x = m_size;		 y = 2 * m_size;	break; // -y
			}

			const img::ImagePtr src = GetSourceData()->GetSubImage( x, y, m_size, m_size );
			return img::utils::ResampleBicubic( src, GetSize(), GetSize() );
		}
		else if( m_sourceTextureType == CST_CrossVertical )
		{
			Uint16 x = 0, y = 0;
			switch (texSlice)
			{
			case 0: x = 2 * m_size;	 y = m_size;		break; // +x
			case 1: x = 0;			 y = m_size;		break; // -x
			case 2: x = m_size;		 y = m_size;		break; // +z
			case 3: x = m_size;		 y = 3 * m_size;	break; // -z
			case 4: x = m_size;		 y = 0;				break; // +y
			case 5: x = m_size;		 y = 2 * m_size;	break; // -y
			}

			const img::ImagePtr src = GetSourceData()->GetSubImage(x, y, m_size, m_size);
			return img::utils::ResampleBicubic(src, GetSize(), GetSize());
		}
		else if( m_sourceTextureType == CST_Panorama )
		{
			const img::ImagePtr src = img::utils::Equirectangular2Cube( GetSourceData()->GetImage(), texSlice, GetSize(), GetSize() );
			return img::utils::ResampleBicubic( src, GetSize(), GetSize() );
		}
	}

	return img::ImagePtr();
}

Uint8 CCubeTexture::GetMaxMipCount() const
{
	return CalcMipCount( GetSize(), GetSize(), 1, true );
}
#endif // RED_CONFIGURATION_FINAL
