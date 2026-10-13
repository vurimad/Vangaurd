/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "textureArray.h"
#include "bitmapTexture.h"

// Core
#include "../../core/include/dataError.h"
#include "../../redReflection/include/variant.h"
#include "../../redReflection/include/serializationLoader.h"

// Render data
#include "../../renderData/include/renderer.h"
#include "../../renderData/include/renderResource.h"
#include "../../redReflection/include/util.h"

using red::DynArray;
using red::String;

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

RTTI_BEGIN_TYPE( CTextureArrayEntry );
{
	RTTI_PROPERTY( m_texture ).setName( "m_texture" ).editable();
}
RTTI_END_TYPE();


RTTI_BEGIN_TYPE( CTextureArray );
{
	RTTI_PARENT_TYPE( ITexture );
	// What to do about this? Shouldn't be cooked, but having mix of cooked/uncooked data might not work (need to be able to get texture names
	// for terrain collision). For now leaving this in -- it's just handles to the bitmaps, so they don't get loaded until they're used.
	RTTI_PROPERTY( m_setup );
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_bitmaps ).notCooked().editable();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_renderResourceBlob );
	RTTI_PROPERTY( m_renderTextureResource );
}
RTTI_END_TYPE();

//***************************************************************************************************************************//
//***************************************************************************************************************************//

CTextureArrayEntry::CTextureArrayEntry() 
{}

CTextureArrayEntry::CTextureArrayEntry( const THandle< CBitmapTexture >& texture ) 
	: m_texture( texture )
{ 

}

CTextureArrayEntry::~CTextureArrayEntry()
{}

//***************************************************************************************************************************//
//***************************************************************************************************************************//

CTextureArray::~CTextureArray()
{
#ifdef USE_RESOURCE_METRICS_BANK
	if( m_renderResource && GResourceLoader && m_resourcePath.IsValid() )
	{
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
		resourceMetricsBank->OnUnloadTexture( m_resourcePath );
	}
#endif
}

void CTextureArray::GetAdditionalInfo( DynArray< String >& info ) const
{
	TBaseClass::GetAdditionalInfo( info );
}

void CTextureArray::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
{
	TBaseClass::OnPropertyPostChange( path, oldValue, value );

	(void)GetRenderResource();
}

Bool CTextureArray::OnPropertyMissing( CName propertyName, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "platformMipBiasXBOX" ) )
	{
		// Take from Xbox or PS4 - doesn't matter. Both have comparable memory and artists never differentiated.d
		readValue.Get<Uint8>( m_setup.m_platformMipBiasConsole );
		return true;
	}
	return TBaseClass::OnPropertyMissing( propertyName, readValue );
}

void CTextureArray::OnPreSave( const PreSaveContext& context )
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

const THandle< IRenderResourceBlob >& CTextureArray::GetRenderResourceBlob() const
{
	if ( m_renderTextureResource.GetRenderResourceBlob() )
	{
		return m_renderTextureResource.GetRenderResourceBlob();
	}

	// #todo: remove fallback after resave
	return m_renderResourceBlob;
}

void CTextureArray::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	if ( !context.flags.skipUploadRenderData )
	{
		// Will almost certainly crash soon anyway
		//RED_FATAL_ASSERT( GetRenderResourceBlob(), "Render resource blob is not available for texture array: %hs.", GetFriendlyName().AsChar() );

		(void)GetRenderResource();
	}
}

#ifndef NO_DATA_VALIDATION
void CTextureArray::OnCheckDataErrors() const
{
	// Pass to base class
	TBaseClass::OnCheckDataErrors();

	// Texture is empty
	if ( !m_bitmaps.Size() )
	{
		DATA_HALT( DES_Major, this, ("Rendering"), ("TextureArray is empty and has no data") );
	}

	for ( Uint32 i = 0; i < m_bitmaps.Size(); ++i )
	{
		if ( !m_bitmaps[i].m_texture.Get() )
		{
			DATA_HALT( DES_Major, this, ("Rendering"), ("TextureArray could not load %-th texture"), i );
		}
	}

	// check consistency
	DynArray< Uint32 > widths{ rend::PoolTexture() };
	DynArray< Uint32 > heights{ rend::PoolTexture() };
	DynArray< ETextureCompression > compressions{ rend::PoolTexture() };
	for ( Uint32 i = 0; i < m_bitmaps.Size(); ++i )
	{
		auto tex = m_bitmaps[i].m_texture.Get();
		if ( tex )
		{
			auto sizeDesc = tex->GetSizeDesc();

			red::alg::PushBackUnique( widths, sizeDesc.width );
			red::alg::PushBackUnique( heights, sizeDesc.height );
			red::alg::PushBackUnique( compressions, tex->GetTextureSetup().m_compression );
		}
	}
	
	if ( widths.Size() > 1 )
	{
		String error( ("Textures of different widths found in TextureArray $path$: ") );
		for ( Uint32 i = 0; i < widths.Size(); ++i )
		{
			error += String::Printf( ( "%d, " ), widths[i] );
		}
		DATA_HALT( DES_Major, this, ("Rendering"), error.AsChar() );
	}
	if ( heights.Size() > 1 )
	{
		String error( ("Textures of different heights found in TextureArray $path$: ") );
		for ( Uint32 i = 0; i < widths.Size(); ++i )
		{
			error += String::Printf( ( "%d, " ), widths[i] );
		}
		DATA_HALT( DES_Major, this, ("Rendering"), error.AsChar() );
	}
	if ( compressions.Size() > 1 )
	{
		String error( ("More than one compression type found in TextureArray $path$: ") );

		const rtti::EnumType* compressionEnum = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR_NOREG( "ETextureCompression" ) );
		ASSERT( compressionEnum );
		for ( Uint32 i = 0; i < compressions.Size(); ++i )
		{
			CName compressionName;
			RED_VERIFY( compressionEnum->FindName( compressions[ i ], compressionName ) );
			error += String::Printf( ( "%hs, " ), compressionName.AsChar() );
		}
		DATA_HALT( DES_Major, this, ("Rendering"), error.AsChar() );
	}

	// Failed to create rendering resource
	if ( !m_renderResource )
	{
		DATA_HALT( DES_Uber, this, ("Rendering"), ("TextureArray was not uploaded to GPU. Ask programmers.") );
	}
}
#endif // NO_DATA_VALIDATION

void CTextureArray::CreateRenderResource()
{
	// Create driver version of texture
	const auto& resourceBlob = GetRenderResourceBlob();
	if ( GRender && !GRender->IsDeviceLost() && resourceBlob )
	{
		

		m_resourcePath = GetPath();
		if( !m_resourcePath.IsValid() )
		{
			if( auto* parent = GetParent() )
			{
				m_resourcePath = parent->GetPath();
			}
		}
#ifdef USE_RESOURCE_METRICS_BANK

		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
		if( m_renderResource )
		{
			resourceMetricsBank->OnUnloadTexture( m_resourcePath );
		}
#endif

		m_renderResourceBlobSize = resourceBlob->GetDataSize();

		// Create rendering resource
		m_renderResource = GRender->UploadBlobTexture( SRenderDataSource{ GetPath(), m_renderResource }, resourceBlob, m_setup );

#ifdef USE_RESOURCE_METRICS_BANK
		if( m_renderResource && m_resourcePath.IsValid() )
		{
			res::SingleResourceMetrics memoryMetrics{};
			memoryMetrics.path = m_resourcePath;
			const Uint32 usedVideoMemory = m_renderResource->GetUsedVideoMemory();
			memoryMetrics.gpuBytesUsed = usedVideoMemory > 0 ? usedVideoMemory : m_renderResourceBlobSize;
			memoryMetrics.texture.group = GetTextureGroup( GetTextureSetup().m_group );
			resourceMetricsBank->OnLoadTexture( memoryMetrics );
		}
#endif
	}
}

void CTextureArray::ReleaseRenderResource()
{
	RED_SCOPE_LOCK( m_resourceCreationLock );

#ifdef USE_RESOURCE_METRICS_BANK
	if( m_renderResource && GResourceLoader && m_resourcePath.IsValid() )
	{
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
		resourceMetricsBank->OnUnloadTexture( m_resourcePath );
	}
#endif

	// Release previous texture array
	m_renderResource = nullptr;
}

CTextureArray::CTextureArray()
	: m_renderResourceBlobSize( 0 )
{
}

const ITexture::TResourcePtr CTextureArray::GetRenderResource() const
{
	RED_SCOPE_LOCK( m_resourceCreationLock );

	if ( !m_renderResource && GRender && !GRender->IsDeviceLost() )
	{
		const_cast<CTextureArray*>( this )->CreateRenderResource();
	}

	return m_renderResource;
}

#ifndef RED_CONFIGURATION_FINAL
Uint8 CTextureArray::GetMaxMipCount() const
{
	// Find the minimum number of mips found in a texture.
	Uint8 numMips = 0;
	for ( Uint32 i = 0; i < m_bitmaps.Size(); ++i )
	{
		if ( auto tex = m_bitmaps[i].m_texture.Get() )
		{
			numMips = Max(tex->GetMaxMipCount(), numMips);
		}
	}
	return numMips;
}
#endif // RED_CONFIGURATION_FINAL

ITexture::SizeDesc CTextureArray::GetSizeDesc() const
{
#ifndef RED_CONFIGURATION_FINAL
	for( Uint32 i = 0; i < m_bitmaps.Size(); ++i )
	{
		auto tex = m_bitmaps[i].m_texture.Get();
		if( tex != nullptr )
		{
			auto sizeDesc = tex->GetSizeDesc();
			sizeDesc.depth = m_bitmaps.Size();
			return sizeDesc;
		}
	}
#endif // RED_CONFIGURATION_FINAL

	return{ (Uint16)0, (Uint16)0, (Uint16)0, (Uint8)0 };
}

void CTextureArray::GetTextureNames( DynArray< CName >& textureNames ) const
{
#ifndef RED_CONFIGURATION_FINAL
	for ( Uint32 i = 0; i < m_bitmaps.Size(); ++i )
	{
		const auto fileName = red::paths::ExtractFileStem( m_bitmaps[i].m_texture.GetPath().ToStringView() );
		textureNames.PushBack( RED_NAME( fileName ) );
	}
#endif // RED_CONFIGURATION_FINAL
}

Bool CTextureArray::AddTexture( const THandle< CBitmapTexture >& texture )
{
#ifdef RED_CONFIGURATION_FINAL
	return false;
#else
	if ( m_bitmaps.Empty() )
	{
		m_setup	= texture->GetTextureSetup();
	}
	else if( m_setup.m_rawFormat != texture->GetTextureSetup().m_rawFormat )
	{
		RED_LOG_ERROR( "Engine: Texture couldn't be added cause array stores different format textures." );
		return false;
	}

	m_bitmaps.PushBack( CTextureArrayEntry( texture ) );
	return true;
#endif // RED_CONFIGURATION_FINAL
}

void CTextureArray::InsertTexture( const THandle< CBitmapTexture >& texture, Uint32 index )
{
#ifndef RED_CONFIGURATION_FINAL
	RED_ASSERT( index >= 0 );
	m_bitmaps.InsertAt( index, CTextureArrayEntry( texture ) );
#endif // RED_CONFIGURATION_FINAL
}

Bool CTextureArray::RemoveTextureAt( Uint32 index )
{
#ifndef RED_CONFIGURATION_FINAL	
	RED_ASSERT( index >= 0 );
	RED_ASSERT( index < m_bitmaps.Size() );
	return m_bitmaps.RemoveAt( index ).IsSuccessful();
#else
	return false;
#endif // RED_CONFIGURATION_FINAL
}

bool CTextureArray::RemoveTexture( const res::ResourcePath& texturePath )
{
#ifndef RED_CONFIGURATION_FINAL	
	for ( Uint32 i = 0; i < m_bitmaps.Size(); i++ )
	{
		const CTextureArrayEntry& entry = m_bitmaps[i];
		if ( entry.m_texture->GetPath() == texturePath )
		{
			return m_bitmaps.RemoveAt( i ).IsSuccessful();
		}
	}
#endif // RED_CONFIGURATION_FINAL	
	return false;
}

Uint32 CTextureArray::GetRenderResourceSize() const
{
	const auto& renderResource = GetRenderResourceBlob();
	return renderResource ? renderResource->GetDataSize() : m_renderResourceBlobSize;
}

void CTextureArray::SetTextures( const DynArray< THandle< CBitmapTexture > >& textures )
{
#ifndef RED_CONFIGURATION_FINAL	
	m_bitmaps.Clear();
	for ( Uint32 i = 0; i < textures.Size(); ++i )
	{
		AddTexture( textures[i] );
	}
#endif // RED_CONFIGURATION_FINAL	
}

void CTextureArray::InitRenderResourceBlob( ECookingPlatform platform, const THandle< rend::IRenderTextureBlob >& blob )
{
	m_renderTextureResource.InitRenderResourceBlob( platform, blob );
	m_renderResourceBlobSize = blob->GetDataSize();
	m_renderResourceBlob.Reset();

	MarkModified();
}

bool CTextureArray::Cooking_OptimiseRenderResourceBlob( ECookingPlatform platform )
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

#ifndef RED_CONFIGURATION_FINAL	
THandle< CBitmapTexture > CTextureArray::GetTexture( Uint32 index ) const 
{
	return index < m_bitmaps.Size() ? m_bitmaps[index].m_texture.Get() : nullptr; 
}
#endif // RED_CONFIGURATION_FINAL	

//***************************************************************************************************************************//
//***************************************************************************************************************************//
