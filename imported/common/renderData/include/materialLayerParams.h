/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

//----------------------------------------

struct MaterialLayerParams
{
	// Layer parameters								// Reg 0
	Vector3			m_tintColor;						// 
	Uint32			m_PADDING;						// 

	Float			m_layerTile;						// Reg 1
	Float			m_microblendTile;				// 
	Float			m_microblendContrast;			// 
	Float			m_microblendStrength;			// 

	Float			m_layerOpacity;					//  Reg 2
	Float			m_layerOffsetU;					// 
	Float			m_layerOffsetV;					// 
	Uint32			m_PADDING2;						// 

	struct SIndirect
	{
		Uint16		m_indirectIndex;
		Uint16		m_arrayType;

		SIndirect()
			: m_indirectIndex( UINT16_MAX )
			, m_arrayType( UINT16_MAX )
		{}
	};

	SIndirect		m_textures[4];					// Reg 3

	MaterialLayerParams()
		: m_tintColor( 1.0f, 1.0f, 1.0f )
		, m_layerTile( 1.0f )
		, m_microblendTile( 1.0f )
		, m_microblendContrast( 1.0f )
		, m_microblendStrength( 1.0f )
		, m_layerOpacity(1.0f)
		, m_layerOffsetU(0.0f)
		, m_layerOffsetV(0.0f)
	{}
};

RED_STATIC_ASSERT( sizeof( MaterialLayerParams ) == 4*16 );