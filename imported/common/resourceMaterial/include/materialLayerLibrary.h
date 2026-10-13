/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "../../redReflection/include/resource.h"

class CMaterialInstance;
class CBitmapTexture;

///////////////////////////////////////////////////////////////////////////////////////////
// Container structure. Will define layers and parameters
///////////////////////////////////////////////////////////////////////////////////////////

struct MaterialLayerDef
{
	RTTI_DECLARE_TYPE( MaterialLayerDef )

	CName						 m_name;					//< 
	Uint32						 m_size;					//< 
	red::DynArray< Color >		 m_colorPalette{ rend::PoolMaterials() };			//< 
	TResRef< CMaterialInstance > m_material;				//< 

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

	MaterialLayerDef()
		: m_size( 0 )
	{}

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

	RED_INLINE Color GetColorTint( Uint32 index ) const
	{
		RED_ASSERT( index < m_colorPalette.Size() );
		return m_colorPalette[index];
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - 
};

struct MicroblendDef
{
	RTTI_DECLARE_TYPE( MicroblendDef )

	CName						m_name;						//<
	TResRef< CBitmapTexture >	m_texture;					//<

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

	MicroblendDef()
	{}

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

};

///////////////////////////////////////////////////////////////////////////////////////////
// Stores all material definitions for layered material
///////////////////////////////////////////////////////////////////////////////////////////

class RESOURCEMATERIAL_API CMaterialLayerLibrary : public CResource
{
	RTTI_DECLARE_TYPE( CMaterialLayerLibrary );
	IMPLEMENT_RESOURCE_INTERFACE( "matlib", "matlib", "Material Layer Library" );
	RED_USE_MEMORY_POOL( rend::PoolMaterials );

public:

	CMaterialLayerLibrary();

	~CMaterialLayerLibrary();

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

	//! Getters

	RED_INLINE Float		GetMatTiling() const { return m_uvTiling; }
	RED_INLINE Float		GetMicroblendTiling() const { return m_mbTiling; }
	RED_INLINE Float		GetMicroblendContrast() const { return m_microblendContrast; }
	RED_INLINE Float		GetMicroblendNormalStrength() const { return m_microblendNormalStrength; }
	RED_INLINE Uint32		GetPaletteColorIndex() const { return m_paletteColorIndex; }

	//! Setters

	RED_INLINE void			SetMatTiling( Float tiling ){ m_uvTiling = tiling; }
	RED_INLINE void			SetMicroblendTiling( Float tiling ){ m_mbTiling = tiling; }
	RED_INLINE void			SetMicroblendContrast( Float contrast ){ m_microblendContrast = contrast; }
	RED_INLINE void			SetMicroblendNormalStrength( Float strength ){ m_microblendNormalStrength = strength; }
	RED_INLINE void			SetPaletteColorIndex( Uint32 index ){ m_paletteColorIndex = index; }
	
	RED_INLINE const MaterialLayerDef* GetLayerEntry( CName layerID ) const 
	{
		auto var = std::find_if( m_layers.Begin(), m_layers.End(), [ layerID ]( const MaterialLayerDef& entr ){ return entr.m_name == layerID; } );
		if( var != m_layers.End() )
		{
			return (const MaterialLayerDef*)var.operator->();
		}

		return nullptr;
	}
	
	RED_INLINE const MicroblendDef* GetMicroblendEntry( CName mbID ) const 
	{
		auto var = std::find_if( m_microblends.Begin(), m_microblends.End(), [ mbID ]( const MicroblendDef& entr ){ return entr.m_name == mbID; } );
		if( var != m_microblends.End() )
		{
			return (const MicroblendDef*)var.operator->();
		}

		return nullptr;
	}

	RED_INLINE red::DynArray< MaterialLayerDef >&		GetMaterialLayers(){ return m_layers; }
	RED_INLINE red::DynArray< MicroblendDef >&		GetMicroblends(){ return m_microblends; }

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

	//! Clears all resources to its initial 'empty' state
	void					ClearResources();

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

protected:

	// Default parameters
	Float								m_uvTiling;							//<
	Float								m_mbTiling;							//<
	Float								m_microblendContrast;				//<
	Float								m_microblendNormalStrength;			//<
	Uint32								m_paletteColorIndex;				//<

	red::DynArray< MaterialLayerDef >	m_layers{ rend::PoolMaterials() };							//<
	red::DynArray< MicroblendDef >		m_microblends{ rend::PoolMaterials() };						//<
};