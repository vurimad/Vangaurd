/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../renderData/include/materialLayerParams.h"

struct MaterialLayerParamOverrides
{
	// Layer parameter overrides
	std::pair< Uint32,  Bool >	m_paletteColorIndex;
	std::pair< Float,   Bool >	m_layerTile;
	std::pair< Color,   Bool >	m_color;
	std::pair< Float,   Bool >	m_microblendTile;
	std::pair< Float,   Bool >  m_microblendContrast;
	std::pair< Float,   Bool >	m_microblendStrength;
	std::pair< Float,	Bool >	m_layerOpacity;
	std::pair< Float,	Bool >	m_layerOffsetU;
	std::pair< Float,	Bool >	m_layerOffsetV;
};

struct RESOURCEMATERIAL_API MaterialLayerDefinition
{
public:
	MaterialLayerDefinition();
	~MaterialLayerDefinition();

	// Layer Definition
	CName			m_layerName;
	CName			m_microblendName;

	// Layer parameters
	MaterialLayerParamOverrides m_params;
};

typedef red::DynArray< MaterialLayerDefinition > TMaterialLayersArray;