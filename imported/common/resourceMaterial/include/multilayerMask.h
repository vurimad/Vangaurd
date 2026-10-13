/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redReflection/include/resource.h"
#include "../../renderData/include/renderMultilayerMaskBlob.h"
#include "../../renderData/include/renderObjectPtr.h"

class CSourceTexture;

class RESOURCEMATERIAL_API Multilayer_Mask : public CResource
{
	RTTI_DECLARE_TYPE( Multilayer_Mask );
	RED_BASE_CLASS( CResource );
	IMPLEMENT_RESOURCE_INTERFACE( "mlmask", "mlmask", "Multilayer Mask" );
	RED_USE_MEMORY_POOL(rend::PoolTexture);

public:
	Multilayer_Mask();
	virtual ~Multilayer_Mask();

	virtual void OnPostLoad( const PostLoadContext& context ) override;

	TRenderPtr<IRenderResource> GetRenderResource() const;
	RED_INLINE const rend::RenderMultilayerMaskResource GetRenderMultilayerMaskResource() const { return m_renderResourceBlob; }

	void InitRenderResourceBlob( ECookingPlatform platform, const THandle< rend::RenderMultilayerMaskBlob >& blob );

	bool Cooking_OptimiseRenderResourceBlob( ECookingPlatform platform );

	virtual Uint32 GetRenderResourceSize() const override;

	virtual Uint32 GetGPUSize() const override;

#ifndef RED_CONFIGURATION_FINAL
	Uint32 GetGPUSize( ECookingPlatform platform ) const;

	Uint32 GetMaxBias() const;
	Uint32 GetMaxLayerBias( Uint32 layer ) const;

	// -1 -- We don't store a value for layer 0, so need to offset the index.
	Uint32 GetLayerBias( Uint32 layer, ECookingPlatform platform ) const;
	// Set bias for given layer. Cannot set bias for layer 0, since it has only implicit data.
	void SetLayerBias( Uint32 layer, ECookingPlatform platform, Uint32 bias );

	Uint32 GetLayerCount() const;

	// Get source data for layers 1..N. Does not include layer 0, which is always fully opaque.
	void GetSourceData(red::DynArray<THandle<CSourceTexture>>& outArray) const { outArray = m_sourceData; }

	// Set source data for layers 1..N. Should get LayerCount-1 elements.
	void SetSourceData( const red::ArraySpan<const THandle<CSourceTexture>>& layers );
#endif // RED_CONFIGURATION_FINAL

	void CreateRenderResource();

private:
	void CreateRenderResource_NoLock();

private:
	rend::RenderMultilayerMaskResource		m_renderResourceBlob;
	Uint32									m_renderResourceBlobSize;
	Bool									m_didRegisterWithResourceMetrics;
	res::ResourcePath						m_resourcePath;
	TRenderPtr<IRenderResource>				m_renderResource;

#ifndef RED_CONFIGURATION_FINAL
	// Biases for layers 1..N-1. layer 0 is not included, since it is implicit and bias doesn't make sense there.
	red::DynArray<Uint32>					m_layerBiases{ rend::PoolTexture() };
	red::DynArray<Uint32>					m_layerBiasesConsole{ rend::PoolTexture() };

	// Source data for layers 1..N-1. layer 0 is not included, it is always fully opaque.
	red::DynArray<THandle<CSourceTexture>> 	m_sourceData{ rend::PoolTexture() }; // REVIEW VERIFY: is this the correct pool?
#endif // RED_CONFIGURATION_FINAL

	mutable red::RWSpinLock m_resourceCreationLock;
};
