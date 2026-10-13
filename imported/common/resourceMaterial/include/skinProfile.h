/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/resource.h"
#include "../../renderData/include/renderTextureBlob.h"
#include "../../renderData/include/renderObjectPtr.h"

class RESOURCEMATERIAL_API CSkinProfile : public CResource
{
	RTTI_DECLARE_TYPE( CSkinProfile );
	IMPLEMENT_RESOURCE_INTERFACE( "sp", "sp", "Skin profile" );
	RED_USE_MEMORY_POOL( rend::PoolRenderResources );

protected:
	Float								m_blurSize;
	Color								m_diffuse;
	Color								m_falloff;
	Float								m_roughness0;
	Float								m_roughness1;
	Float								m_lobeMix;
	TRenderPtr<IRenderResource>			m_renderResource;
	mutable red::RWSpinLock				m_resourceCreationLock;

public:
	CSkinProfile();
	virtual ~CSkinProfile();

	// Create rendering resource for material
	virtual void CreateRenderResource();

	// Update rendering resource for material
	virtual void UpdateRenderResource();

	// Get the rendering proxy for this material
	const TRenderPtr<IRenderResource>& GetRenderResource() const;

	// Does the material currently have a render resource created?
	RED_INLINE Bool HasRenderResource() const { return m_renderResource != nullptr; }
};