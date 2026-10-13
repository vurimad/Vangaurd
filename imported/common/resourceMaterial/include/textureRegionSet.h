/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/resource.h"
#include "../../renderData/include/renderTextureBlob.h"
#include "../../renderData/include/renderSkinData.h"
#include "../../renderData/include/renderObjectPtr.h"

class RESOURCEMATERIAL_API CTextureRegionSet : public CResource
{
	RTTI_DECLARE_TYPE( CTextureRegionSet );
	IMPLEMENT_RESOURCE_INTERFACE( "regionset", "regionset", "Texture Region Set" );
	RED_BASE_CLASS( CResource );
	RED_USE_MEMORY_POOL( rend::PoolRenderResources );

public:
	CTextureRegionSet();
	virtual ~CTextureRegionSet();

public:
	RED_FORCE_INLINE void AppendRegion( const rend::TextureRegion& region ) { m_regions.PushBack( region ); }

public:
	// Create rendering resource for material
	virtual void CreateRenderResource();

	// Update rendering resource for material
	virtual void UpdateRenderResource();

	// Get the rendering proxy for this material
	const TRenderPtr<IRenderResource>& GetRenderResource() const;

	// Does the material currently have a render resource created?
	RED_INLINE Bool HasRenderResource() const { return m_renderResource != nullptr; }

protected:
	red::DynArray< rend::TextureRegion >	m_regions{ rend::PoolRenderResources() };
	TRenderPtr<IRenderResource>		m_renderResource;
};