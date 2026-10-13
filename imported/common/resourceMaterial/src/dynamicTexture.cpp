/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "dynamicTexture.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderer.h"
#include "../../renderData/include/renderCommandHandler.h"
#include "../../renderData/include/renderDynamicTextureDesc.h"
#include "../../resourceMaterial/include/material.h"
#include "../../redReflection/include/resourceMetricsBank.h"
#include "../../redReflection/include/variant.h"

RTTI_BEGIN_TYPE( DynamicTexture );
{
	RTTI_PARENT_TYPE( ITexture );

	RTTI_PROPERTY_CATEGORY( "Size" );
	RTTI_PROPERTY( m_width ).editable().range( 0, 2048 ).tooltip( "Set to 0 if you want to overwrite the texture size in RenderToTextureComponent." );
	RTTI_PROPERTY( m_height ).editable().range( 0, 2048 ).tooltip( "Set to 0 if you want to overwrite the texture size in RenderToTextureComponent." );
	RTTI_PROPERTY( m_scaleToViewport ).editable().tooltip( "Setting this option will force texture to use biggest possible resolution preserving aspect ratio." );
	RTTI_PROPERTY( m_mipChain ).editable().tooltip( "Does the texture have a mipmaps chain?" );
	RTTI_PROPERTY( m_samplesCount ).editable().range( 1, 8 ).tooltip( "MSAA samples count" );
	RTTI_PROPERTY( m_dataFormat ).editable();
	RTTI_PROPERTY( m_generator ).editable().inlined();
}
RTTI_END_TYPE();

RTTI_BEGIN_ENUM(DynamicTextureDataFormat)
{
	RTTI_ENUM_OPTION( R_Uint8);
	RTTI_ENUM_OPTION( R_Float16);
	RTTI_ENUM_OPTION( R_Float32);
	RTTI_ENUM_OPTION( RG_Float16);
	RTTI_ENUM_OPTION( RG_Float32);
	RTTI_ENUM_OPTION( RGBA_Uint8);
	RTTI_ENUM_OPTION( RGBA_Uint8_SRGB);
	RTTI_ENUM_OPTION( RGBA_Float16);
	RTTI_ENUM_OPTION( RGBA_Float32);
}
RTTI_END_ENUM();

//***************************************************************************************************************************//
//***************************************************************************************************************************//

static GpuWrapApi::eTextureFormat Map( DynamicTextureDataFormat format )
{
	switch (format)
	{
	case DynamicTextureDataFormat::R_Uint8:				return GpuWrapApi::TEXFMT_R8_Unorm;
	case DynamicTextureDataFormat::RGBA_Uint8:			return GpuWrapApi::TEXFMT_R8G8B8A8_Unorm;
	case DynamicTextureDataFormat::RGBA_Uint8_SRGB:		return GpuWrapApi::TEXFMT_R8G8B8A8_Unorm_SRGB;
	case DynamicTextureDataFormat::R_Float16:			return GpuWrapApi::TEXFMT_R16_Float;
	case DynamicTextureDataFormat::R_Float32:			return GpuWrapApi::TEXFMT_R32_Float;
	case DynamicTextureDataFormat::RG_Float16:			return GpuWrapApi::TEXFMT_R16G16_Float;
	case DynamicTextureDataFormat::RG_Float32:			return GpuWrapApi::TEXFMT_R32G32_Float;
	case DynamicTextureDataFormat::RGBA_Float16:		return GpuWrapApi::TEXFMT_R16G16B16A16_Float;
	case DynamicTextureDataFormat::RGBA_Float32:		return GpuWrapApi::TEXFMT_R32G32B32A32_Float;
	}

	RED_FATAL( "Unhandled format" );
	return GpuWrapApi::TEXFMT_R8G8B8A8_Unorm;
}

//***************************************************************************************************************************//
//***************************************************************************************************************************//

class CRenderTextureMaterial : public IDynamicTextureGenerator
{
	RTTI_DECLARE_TYPE(CRenderTextureMaterial);

public:

	TResRef<IMaterial>				m_material;

public:

	CRenderTextureMaterial()
	{}

	~CRenderTextureMaterial()
	{}

	virtual void OnAttach(IRenderResource* res, Uint32 width, Uint32 height) override
	{

	}

	virtual void OnDetach() override
	{

	}

	virtual void Generate( IRenderResource* res ) override
	{
		if ( m_material )
		{
			GRender->DrawQuadWithMaterial( res, m_material->GetRenderResource().Get() );
		}
	}

};

RTTI_BEGIN_TYPE(CRenderTextureMaterial);
{
	RTTI_PARENT_TYPE(IDynamicTextureGenerator);
}
RTTI_END_TYPE();

//***************************************************************************************************************************//
//***************************************************************************************************************************//

DynamicTexture::DynamicTexture()
	: m_width( 256 )
	, m_height( 256 )
	, m_dynamicWidth( 0 )
	, m_dynamicHeight( 0 )
	, m_mipChain( false )
	, m_samplesCount( 1 )
	, m_dataFormat( DynamicTextureDataFormat::RGBA_Uint8_SRGB )
{
	m_setup.m_group = GpuWrapApi::TEXG_Unknown;
}

DynamicTexture::DynamicTexture( const InitInfo& initInfo )
	: m_width( initInfo.width )
	, m_height( initInfo.height )
	, m_dynamicWidth( 0 )
	, m_dynamicHeight( 0 )
	, m_mipChain( initInfo.mipChain )
	, m_samplesCount( initInfo.samplesCount )
	, m_dataFormat( initInfo.format )
	, m_renderResource( initInfo.renderResource )
	, m_renderTargetResource( initInfo.renderResource )
{
	m_setup.m_group = initInfo.group;
}

DynamicTexture::~DynamicTexture()
{
#ifdef USE_RESOURCE_METRICS_BANK
	if ( GResourceLoader && m_renderResource && m_resourcePath.IsValid() )
	{
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
		resourceMetricsBank->OnUnloadTexture( m_resourcePath );
	}
#endif
}

void DynamicTexture::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	if ( !context.flags.skipUploadRenderData )
	{
		RED_SCOPE_LOCK( m_resourceCreationLock );
		RecreateRenderResource_NoLock();
	}
}

Bool DynamicTexture::OnPropertyMissing(CName propertyName, const rtti::Variant& readValue)
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "material" ) )
	{
		TResRef<IMaterial> oldMaterial;
	
		if (!readValue.Get(oldMaterial))
			return false;
	
		auto generator = CreateHandle< CRenderTextureMaterial >();
		generator->m_material = oldMaterial;

		m_generator = generator;
	}

	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "format" ) )
	{
		GpuWrapApi::eTextureFormat oldFormat = GpuWrapApi::TEXFMT_R8G8B8A8_Unorm;

		if ( !readValue.Get(oldFormat) )
			return false;

		switch (oldFormat)
		{
		case GpuWrapApi::TEXFMT_R8_Unorm:						m_dataFormat = DynamicTextureDataFormat::R_Uint8;		break;
		case GpuWrapApi::TEXFMT_R8G8B8A8_Unorm:				m_dataFormat = DynamicTextureDataFormat::RGBA_Uint8;	break;
		case GpuWrapApi::TEXFMT_R16_Float:				m_dataFormat = DynamicTextureDataFormat::R_Float16;		break;
		case GpuWrapApi::TEXFMT_R32_Float:				m_dataFormat = DynamicTextureDataFormat::R_Float32;		break;
		case GpuWrapApi::TEXFMT_R16G16_Float:			m_dataFormat = DynamicTextureDataFormat::RG_Float16;	break;
		case GpuWrapApi::TEXFMT_R32G32_Float:			m_dataFormat = DynamicTextureDataFormat::RG_Float32;	break;
		case GpuWrapApi::TEXFMT_R16G16B16A16_Float:		m_dataFormat = DynamicTextureDataFormat::RGBA_Float16;	break;
		case GpuWrapApi::TEXFMT_R32G32B32A32_Float:		m_dataFormat = DynamicTextureDataFormat::RGBA_Float32;	break;

		default: RED_WARNING( 0, "Unable to map dynamic texture format. Fallback to default RGBA_Uint8" );
		}
	}

	return true;
}

void DynamicTexture::ApplyDynamicSize( Uint32 width, Uint32 height )
{
	if ( HasDynamicSize() || HasScaleToViewport() )
	{
		RED_SCOPE_LOCK( m_resourceCreationLock );

		if ( m_dynamicWidth != width || m_dynamicHeight != height )
		{
			m_dynamicWidth = width;
			m_dynamicHeight = height;

			if ( m_renderResource )
			{
				RecreateRenderResource_NoLock();
			}
		}
	}
}

const ITexture::TResourcePtr DynamicTexture::GetRenderResource() const
{
	ITexture::TResourcePtr renderResource;

	// try read-only lock first
	{
		RED_SCOPE_SHARED_LOCK( m_resourceCreationLock );
		renderResource = m_renderResource;
	}

	if ( !renderResource )
	{
		RED_SCOPE_LOCK( m_resourceCreationLock );

		if ( !m_renderResource )
		{
			DynamicTexture* nonConstThis = const_cast<DynamicTexture*>( this );
			nonConstThis->RecreateRenderResource_NoLock();
		}

		renderResource = m_renderResource;
	}

	return renderResource;
}

ITexture::TResourcePtr DynamicTexture::GetRenderTargetResource() const
{
	ITexture::TResourcePtr renderTargetResource;

	// try read-only lock first
	{
		RED_SCOPE_SHARED_LOCK( m_resourceCreationLock );
		renderTargetResource = m_renderTargetResource;
	}

	if ( !renderTargetResource )
	{
		RED_SCOPE_LOCK( m_resourceCreationLock );

		if ( !m_renderTargetResource )
		{
			DynamicTexture* nonConstThis = const_cast<DynamicTexture*>( this );
			nonConstThis->RecreateRenderResource_NoLock();
		}

		renderTargetResource = m_renderTargetResource;
	}

	return renderTargetResource;
}

void DynamicTexture::CreateRenderResource()
{

}

void DynamicTexture::ReleaseRenderResource()
{
	RED_SCOPE_LOCK( m_resourceCreationLock );

#ifdef USE_RESOURCE_METRICS_BANK
	auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
	if ( m_renderResource )
	{
		if ( m_resourcePath.IsValid() )
		{
			resourceMetricsBank->OnUnloadTexture( m_resourcePath );
		}
	}
#endif
	m_renderResource.Reset();
	m_renderTargetResource.Reset();
}

Bool DynamicTexture::IsRenderResourceCreated() const
{
	RED_SCOPE_SHARED_LOCK( m_resourceCreationLock );
	return static_cast<Bool>( m_renderResource );
}

const STextureGroupSetup& DynamicTexture::GetTextureSetup() const
{
	return m_setup;
}

void DynamicTexture::RecreateRenderResource_Safe()
{
	RED_SCOPE_LOCK(m_resourceCreationLock);
	RecreateRenderResource_NoLock();
}

void DynamicTexture::RegisterResolutionChangeListener( const THandle<ISerializable>& listener, red::Function< void() >&& callback )
{
	RED_SCOPE_LOCK( m_listenerAccessLock );
	if( listener )
	{
		for( Int32 i = 0; i < ( Int32 )m_listeners.Size(); i++ )
		{
			if( m_listeners[ i ].m_listener.Expired() )
			{
				m_listeners.RemoveAt( i );
				i--;
				continue;
			}

			if( const THandle<ISerializable> innerListener = m_listeners[ i ].m_listener )
			{
				if( innerListener == listener )
				{
					return;
				}
			}
		}

		auto &newCallback = m_listeners.EmplaceBack();
		newCallback.m_callback = std::move( callback );
		newCallback.m_listener = listener;
	}
}

void DynamicTexture::UnRegisterResolutionChangeListener( const THandle<ISerializable>& listener )
{
	RED_SCOPE_LOCK( m_listenerAccessLock );
	if( listener )
	{
		for( Int32 i = 0; i < ( Int32 )m_listeners.Size(); i++ )
		{
			if( m_listeners[ i ].m_listener.Expired() )
			{
				m_listeners.RemoveAt( i );
				i--;
				continue;
			}

			if( const THandle<ISerializable> innerListener = m_listeners[ i ].m_listener )
			{
				if( innerListener == listener )
				{
					m_listeners.RemoveAt( i );
					return;
				}
			}
		}
	}
}

void DynamicTexture::TryAdjustToViewportResolution( Uint32 width, Uint32 height )
{
	if( HasScaleToViewport() )
	{
		Float scaleX, scaleY; // scale factor used for consoles
		GRender->GetResolutionScale( scaleX, scaleY );

		Vector2 desiredSize( width * scaleX, height * scaleY );

		Float textureAspect = ( Float )m_width / ( Float )m_height;
		Float viewportAspect = ( Float )desiredSize.X / desiredSize.Y;

		Uint32 dynamicWidth, dynamicHeight;
		m_dynamicWidth = m_dynamicHeight = 1;

		// Fill texture to maximum supported resolution
		if( textureAspect > 1.0f ) // Horizontal texture
		{
			if( textureAspect > viewportAspect )
			{
				// Fit to width
				dynamicWidth = ( Uint32 )desiredSize.X;
				dynamicHeight = ( Uint32 )( desiredSize.X / textureAspect );
			}
			else if( textureAspect < viewportAspect )
			{
				// Fit to height
				dynamicWidth = ( Uint32 )( desiredSize.Y * textureAspect );
				dynamicHeight = ( Uint32 )( desiredSize.Y );
			}
			else // Has the same ratio
			{
				dynamicWidth = ( Uint32 )desiredSize.X;
				dynamicHeight = ( Uint32 )desiredSize.Y;
			}
		}
		else if( textureAspect < 1.0f  )// Vertical texture
		{
			dynamicWidth = ( Uint32 )( desiredSize.Y * textureAspect );
			dynamicHeight = ( Uint32 )desiredSize.Y;
		}
		else // Square texture
		{
			dynamicWidth = ( Uint32 )desiredSize.Y;
			dynamicHeight = ( Uint32 )desiredSize.Y;
		}

		ApplyDynamicSize( dynamicWidth, dynamicHeight );

		RED_SCOPE_LOCK( m_listenerAccessLock );
		// Inform listeners about change
		for( Int32 i = 0; i < (Int32)m_listeners.Size(); i++ )
		{
			if( !m_listeners[ i ].m_listener.Expired() )
			{
				m_listeners[ i ].m_callback();
			}
			else
			{
				m_listeners.RemoveAt( i );
				i--;
			}
		}
	}
}

void DynamicTexture::RecreateRenderResource_NoLock()
{
	if( GRender && !GRender->IsDeviceLost() )
	{
		

		m_resourcePath = GetPath();
		if ( !m_resourcePath.IsValid() )
		{
			if ( auto* parent = GetParent() )
			{
				m_resourcePath = parent->GetPath();
			}
		}

		Bool pathValid = m_resourcePath.IsValid();

#ifdef USE_RESOURCE_METRICS_BANK
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
		if ( m_renderResource )
		{
			// Release and then create, instead of just assign new value, so that maybe the previous texture's resources can
			// be reclaimed first.
			m_renderResource.Reset();
			if (pathValid)
			{
				resourceMetricsBank->OnUnloadTexture( m_resourcePath );
			}
		}
#endif

		m_renderTargetResource.Reset();

		DynamicTextureDesc desc;


		if ( HasDynamicSize() || HasScaleToViewport() )
		{
			desc.width = m_dynamicWidth;
			desc.height = m_dynamicHeight;

			if ( m_dynamicHeight == 0 || m_dynamicWidth == 0 )
			{
				RED_WARNING_ONCE( m_dynamicHeight > 0 && m_dynamicWidth > 0, "DynamicTexture: texture size not specified (%hs). Render resource won't be created.", m_resourcePath.ToDebugString() );
				return;
			}
		}
		else
		{
			desc.width = m_width;
			desc.height = m_height;
		}

		// multisampled render target won't have mips, because it has to be resolved anyway
		desc.mipChain	= m_samplesCount > 1 ? false : m_mipChain;
		desc.samplesCount = m_samplesCount;
		desc.format		= Map( m_dataFormat );
		desc.generator	= m_generator;
		desc.group		= m_setup.m_group;

#ifndef RED_CONFIGURATION_FINAL
		desc.debugName = GetPath().ToStringView();
#endif // RED_CONFIGURATION_FINAL

		m_renderTargetResource = GRender->CreateDynamicTexture( desc );

		if ( desc.samplesCount > 1 )
		{
			desc.mipChain = m_mipChain;
			desc.samplesCount = 1;
			m_renderResource = GRender->CreateDynamicTexture( desc );
		}
		else
		{
			m_renderResource = m_renderTargetResource;
		}

		if( !m_renderResource || !m_renderTargetResource )
		{
			RED_LOG_ERROR( "Failed to create dynamic texture : Size ( %d , %d ) | Samples : %d ", m_width, m_height, m_samplesCount );
		}

#ifdef USE_RESOURCE_METRICS_BANK
		if (pathValid && m_renderResource != nullptr)
		{
			res::SingleResourceMetrics memoryMetrics{};
			memoryMetrics.path = m_resourcePath;
			memoryMetrics.gpuBytesUsed = GetRenderResourceSize_NoLock();
			memoryMetrics.texture.group = GetTextureGroup(GetTextureSetup().m_group);

			resourceMetricsBank->OnLoadTexture(memoryMetrics);
		}
#endif
	}
}

Uint32 DynamicTexture::GetRenderResourceSize() const
{
	RED_SCOPE_SHARED_LOCK( m_resourceCreationLock );

	return GetRenderResourceSize_NoLock();
}


Uint32 DynamicTexture::GetRenderResourceSize_NoLock() const
{
	if ( !m_renderResource )
	{
		return 0;
	}

	if ( m_renderResource != m_renderTargetResource )
	{
		RED_FATAL_ASSERT( m_renderTargetResource );
		return m_renderResource->GetUsedVideoMemory() + m_renderTargetResource->GetUsedVideoMemory();
	}

	return m_renderResource->GetUsedVideoMemory();
}
