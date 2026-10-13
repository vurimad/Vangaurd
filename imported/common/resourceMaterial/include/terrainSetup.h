/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/resource.h"
#include "../../renderData/include/renderResource.h"

	class RESOURCEMATERIAL_API CTerrainSetup : public CResource
{
	RTTI_DECLARE_TYPE( CTerrainSetup );
	IMPLEMENT_RESOURCE_INTERFACE( "terrainsetup", "terrainsetup", "Terrain Setup" );
	RED_USE_MEMORY_POOL( rend::PoolTexture );

protected:
	red::DynArray< Float > m_tiling{rend::PoolTexture()};
	red::DynArray< CName > m_physicalMaterial{rend::PoolTexture()};
	TRenderPtr< IRenderResource > m_renderResource;
	mutable red::RWSpinLock m_resourceCreationLock;

public:
	CTerrainSetup();
	virtual ~CTerrainSetup();

	virtual void OnPostLoad( const PostLoadContext& context ) override;

	// Create rendering resource for material
	virtual void CreateRenderResource();

	// Update rendering resource for material
	virtual void UpdateRenderResource();
	virtual void ReleaseRenderResource();

	// Get the rendering proxy for this material
	const TRenderPtr< IRenderResource >& GetRenderResource() const;

	// Does the material currently have a render resource created?
	RED_INLINE Bool HasRenderResource() const { return m_renderResource != nullptr; }

	red::DynArray< Float > GetUvTilings() const { return m_tiling; }
	red::DynArray< CName > GetPhysicalMaterialNames() const { return m_physicalMaterial; }
};
