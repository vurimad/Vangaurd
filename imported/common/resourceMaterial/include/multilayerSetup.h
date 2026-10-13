/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redReflection/include/resource.h"
#include "../../../common/renderData/include/renderResource.h"


class CBitmapTexture;
class Multilayer_LayerTemplate;


struct RESOURCEMATERIAL_API Multilayer_Layer
{
	RTTI_DECLARE_TYPE( Multilayer_Layer );

	Multilayer_Layer();

	Float m_matTile;
	Float m_mbTile;
	TResRef<CBitmapTexture> m_microblend;
	Float m_microblendContrast;
	Float m_microblendNormalStrength;
	Float m_microblendOffsetU;
	Float m_microblendOffsetV;
	Float m_opacity;
	Float m_offsetU;
	Float m_offsetV;

	// #TODO: These should just be for uncooked? In cooked we would collapse the data. Or maybe this whole class would
	// be editor only?
	TResRef<Multilayer_LayerTemplate> m_material;

	// Override selections
	CName m_colorScale;
	CName m_normalStrength;
	CName m_roughLevelsIn;
	CName m_roughLevelsOut;
	CName m_metalLevelsIn;
	CName m_metalLevelsOut;

	// #TODO: Legacy. To be removed.
	CName m_overrides;
};


class RESOURCEMATERIAL_API Multilayer_Setup: public CResource
{
	RTTI_DECLARE_TYPE( Multilayer_Setup );
	IMPLEMENT_RESOURCE_INTERFACE( "mlsetup", "mlsetup", "Multilayer Setup" );
	RED_BASE_CLASS( CResource );
	RED_USE_MEMORY_POOL( rend::PoolMaterials );

public:
	Multilayer_Setup();
	virtual ~Multilayer_Setup();

	TRenderPtr<IRenderResource> GetRenderResource() const;
	void ReleaseRenderResource();

	const red::DynArray< Multilayer_Layer >& GetLayers() const { return m_layers; }

protected:
	virtual void OnPostLoad( const PostLoadContext& context ) override;

private:
	void CreateRenderResource();

	red::DynArray< Multilayer_Layer >						m_layers{ rend::PoolMaterials() };
	red::DynArray< THandle< CBitmapTexture > >				m_layerTextures{ rend::PoolMaterials() };
	red::DynArray< THandle< Multilayer_LayerTemplate > >	m_layerMaterials{ rend::PoolMaterials() };

	Float m_ratio;
	Bool m_useNormal;

	TRenderPtr<IRenderResource> m_renderResource;

	static red::Mutex s_onDemandCreationMutex;
};

class RESOURCEMATERIAL_API CookedMultilayer_Setup : public CResource
{
	RTTI_DECLARE_TYPE( CookedMultilayer_Setup );
	IMPLEMENT_RESOURCE_INTERFACE( "", "cooked_mlsetup", "Cooked Multilayer Setup" );
	RED_BASE_CLASS( CResource );
	RED_USE_MEMORY_POOL( rend::PoolMaterials );

public:

	static red::Atomic<Int32> s_loadingCounter;


	CookedMultilayer_Setup();
	virtual ~CookedMultilayer_Setup();

	void AddDependency( const res::ResourcePath& path );

	static res::ResourcePath GetCookedResourcePath();

private:

	virtual void OnSerialize( IFile& file ) override final;
	virtual void OnPostLoad( const PostLoadContext& context ) override final;

	red::DynArray< TResRef< Multilayer_Setup > > m_dependencies;
	red::DynArray< THandle< CResource > > m_resources;
};
