/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/resource.h"
#include "../../renderData/include/renderTextureBlob.h"
#include "../../renderData/include/renderObjectPtr.h"

namespace rend
{
    struct FoliageProfileData;
}

class CGradient;

class RESOURCEMATERIAL_API CFoliageProfile : public CResource
{
	RTTI_DECLARE_TYPE( CFoliageProfile );
	IMPLEMENT_RESOURCE_INTERFACE( "fp", "fp", "Foliage profile" );
	RED_USE_MEMORY_POOL( rend::PoolRenderResources );

protected:
	//Whenever adding a new param to a profiles' texture, remember to modify MAX_FLOAT4S_PER_PROFILE in CreateFoliageProfileTexture()
	Float	m_cutoffAlphaMinMip;
	Float	m_cutoffAlphaMaxMip;
	Float	m_billboardCutoffAlpha;
	Float	m_aoScale;
	Float	m_terrainBlendScale;
    Float   m_terrainBlendBias;
    Float   m_billboardDepthScale;
    Float   m_preserveOriginalColor;
    Float   m_billboardRoughnessBias;

	//Parameters not included directly in Foliage Texture 
	TResRef<CGradient> m_colorGradient;
	Float m_colorGradientWeight;
	Float m_colorGradientDarkenWeight;

	TRenderPtr<IRenderResource>	m_renderResource;
	mutable red::RWSpinLock		m_resourceCreationLock;

    static void FillFoliageProfileData( rend::FoliageProfileData* data, const CFoliageProfile& fp );

public:
	CFoliageProfile();
	virtual ~CFoliageProfile();

	// Create rendering resource for material
	virtual void CreateRenderResource();

	// Update rendering resource for material
	virtual void UpdateRenderResource();

	// Get the rendering proxy for this material
	const TRenderPtr<IRenderResource>& GetRenderResource() const;

	// Does the material currently have a render resource created?
	RED_INLINE Bool HasRenderResource() const { return m_renderResource != nullptr; }
};