/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "multilayerMask.h"
#include "sourceTextureData.h"
#include "../../renderData/include/renderer.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderMultilayer.h"
#include "../../redReflection/include/resourceMetricsBank.h"

RTTI_BEGIN_TYPE( Multilayer_Mask );
RTTI_PARENT_TYPE( CResource );
{
	RTTI_PROPERTY(m_renderResourceBlob);
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY(m_layerBiases).notCooked();
	RTTI_PROPERTY(m_layerBiasesConsole).notCooked();
	RTTI_PROPERTY(m_sourceData).notCooked();
#endif // RED_CONFIGURATION_FINAL
}
RTTI_END_TYPE();


Multilayer_Mask::Multilayer_Mask()
	: m_renderResourceBlobSize( 0 )
	, m_didRegisterWithResourceMetrics( false )
{
}

Multilayer_Mask::~Multilayer_Mask()
{
#ifdef USE_RESOURCE_METRICS_BANK
	auto resourceBlob = m_renderResourceBlob.GetRenderResourceBlob();
	if ( GResourceLoader && resourceBlob && m_resourcePath.IsValid() && m_didRegisterWithResourceMetrics )
	{
		if ( m_renderResource )
		{
			auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
			resourceMetricsBank->OnUnloadMaterial( m_resourcePath );
		}
	}
#endif
}

void Multilayer_Mask::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	if ( !context.flags.skipUploadRenderData )
	{
		CreateRenderResource_NoLock();                     // No locking needed here, the resource was just loaded and nothing should be using it yet.
	}

#ifndef RED_CONFIGURATION_FINAL
	if ( m_layerBiasesConsole.Size() != m_layerBiases.Size() )
	{
		m_layerBiasesConsole = m_layerBiases;
	}
#endif // RED_CONFIGURATION_FINAL
}

void Multilayer_Mask::InitRenderResourceBlob( ECookingPlatform platform, const THandle< rend::RenderMultilayerMaskBlob >& blob )
{
	m_renderResourceBlob.InitRenderResourceBlob( platform, blob );
	m_renderResourceBlobSize = blob->GetDataSize();

	// Recreate render resource if we have one. If we don't have one, then there's no point in creating a new one.
	RED_SCOPE_LOCK( m_resourceCreationLock );
	if ( m_renderResource != nullptr )
	{
		CreateRenderResource_NoLock();
	}
}

void Multilayer_Mask::CreateRenderResource()
{
	RED_SCOPE_LOCK( m_resourceCreationLock );
	CreateRenderResource_NoLock();
}

void Multilayer_Mask::CreateRenderResource_NoLock()
{
	const auto& resourceBlob = m_renderResourceBlob.GetRenderResourceBlob();
	if ( GRender && !GRender->IsDeviceLost() && resourceBlob )
	{
		m_renderResourceBlobSize = resourceBlob->GetDataSize();

		m_resourcePath = GetPath();
		if ( !m_resourcePath.IsValid()  )
		{
			if ( auto* parent = GetParent() )
			{
				m_resourcePath = parent->GetPath();
			}
		}

#ifdef USE_RESOURCE_METRICS_BANK
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();

		if ( m_renderResource && m_resourcePath.IsValid() && m_didRegisterWithResourceMetrics )
		{
			m_didRegisterWithResourceMetrics = false;
			resourceMetricsBank->OnUnloadMaterial( m_resourcePath );
		}
#endif

		SRenderDataSource dataSource = { GetPath(), m_renderResource };
		m_renderResource = GRender->UploadMultilayerMask( dataSource, resourceBlob );

#ifdef USE_RESOURCE_METRICS_BANK
		if ( m_renderResource && m_resourcePath.IsValid() )
		{
			res::SingleResourceMetrics memoryMetrics{};
			memoryMetrics.path = m_resourcePath;
			memoryMetrics.gpuBytesUsed = m_renderResource->GetUsedVideoMemory() > 0 ? m_renderResource->GetUsedVideoMemory() : m_renderResourceBlobSize;
			resourceMetricsBank->OnLoadMaterial( memoryMetrics );
			m_didRegisterWithResourceMetrics = true;
		}
#endif
	}
}

bool Multilayer_Mask::Cooking_OptimiseRenderResourceBlob( ECookingPlatform platform )
{
	const auto savedBlob = Cast< rend::RenderMultilayerMaskBlob >( m_renderResourceBlob.GetCookingPlatfromResourceBlob( platform ) );
	if ( savedBlob == nullptr )
	{
		RED_LOG_ERROR( "Cooking: Error getting platform specific render texture blob, cooked data will contain data for ALL PLATFORMS!" );
		return false;
	}

	m_renderResourceBlob.Clear();
	m_renderResourceBlob.InitRenderResourceBlob( platform, savedBlob );
	m_renderResourceBlobSize = savedBlob->GetDataSize();

	return true;
}

TRenderPtr<IRenderResource> Multilayer_Mask::GetRenderResource() const
{
	RED_SCOPE_SHARED_LOCK( m_resourceCreationLock );
	return m_renderResource;
}

Uint32 Multilayer_Mask::GetRenderResourceSize() const
{
	RED_SCOPE_SHARED_LOCK( m_resourceCreationLock );
	if ( m_renderResourceBlobSize != 0 )
	{
		return m_renderResourceBlobSize;
	}
	else if ( const auto& renderResource = m_renderResourceBlob.GetRenderResourceBlob() )
	{
		return renderResource->GetDataSize();
	}

	return 0;
}

Uint32 Multilayer_Mask::GetGPUSize() const
{
	// Just return PC blob size for now.
	if ( m_renderResourceBlob.GetRenderResourceBlob() )
	{
		return m_renderResourceBlob.GetRenderResourceBlob()->GetDataSize();
	}
	else
	{
		return 0;
	}
}

#ifndef RED_CONFIGURATION_FINAL
Uint32 Multilayer_Mask::GetGPUSize( ECookingPlatform platform ) const
{
	if ( const THandle< IRenderResourceBlob >& blob = m_renderResourceBlob.GetCookingPlatfromResourceBlob( platform ) )
	{
		return blob->GetDataSize();
	}
	return 0;
}

Uint32 Multilayer_Mask::GetMaxBias() const
{
	Uint32 maxBias = 0;
	for ( Uint32 i = 0; i < m_sourceData.Size(); ++i )
	{
		maxBias = Max( maxBias, GetMaxLayerBias( i+1 ) );
	}
	return maxBias;
}

Uint32 Multilayer_Mask::GetMaxLayerBias( Uint32 layer ) const
{
	if ( layer == 0 )
	{
		return 0;
	}
	RED_ASSERT( layer-1 < m_sourceData.Size(), "Accessing invalid layer %u (have %u layers)", layer, m_sourceData.Size()+1 );

	// Can only bias as long as the dimensions are multiple of 2. If we have a dimension that isn't multiple of 2, then
	// biasing further means not a perfect cut in half, which we don't allow.
	Uint32 maxBias = 0;
	Uint32 w = m_sourceData[layer-1]->GetWidth();
	Uint32 h = m_sourceData[layer-1]->GetHeight();
	while ( w > 1 && h > 1 && (w & 1) == 0 && (h & 1) == 0 )
	{
		++maxBias;
		w >>= 1;
		h >>= 1;
	}
	return maxBias;
}

Uint32 Multilayer_Mask::GetLayerBias( Uint32 layer, ECookingPlatform platform ) const
{
	if ( layer == 0 )
	{
		return 0;
	}
	
	if ( platform == PLATFORM_PC )
	{
		return m_layerBiases[layer - 1];
	}
	return m_layerBiasesConsole[layer - 1];
}

void Multilayer_Mask::SetLayerBias(Uint32 layer, ECookingPlatform platform, Uint32 bias)
{
	RED_ASSERT( layer != 0, "Can't set layer bias for layer 0" );
	// -1 -- Take in actual layer index, but m_layerBiases only stores values for layers 1+ (since biasing layer 0
	// doesn't make sense).

	red::DynArray< Uint32 >& biases = ( platform == PLATFORM_PC ) ? m_layerBiases : m_layerBiasesConsole;
	if (biases[layer-1] != bias)
	{
		biases[layer-1] = bias;
		MarkModified(); // make sure the next time we save this will make it to the file 
	}
}

Uint32 Multilayer_Mask::GetLayerCount() const
{
	// m_sourceData doesn't include layer 0.
	return m_sourceData.Size()+1;
}

void Multilayer_Mask::SetSourceData( const red::ArraySpan<const THandle<CSourceTexture>>& layers )
{
	m_sourceData.Clear();
	m_sourceData.PushBack( layers );
	m_layerBiases.Resize( m_sourceData.Size() );
	m_layerBiasesConsole.Resize( m_sourceData.Size() );
	MarkModified();
}
#endif // RED_CONFIGURATION_FINAL
