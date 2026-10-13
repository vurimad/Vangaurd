/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "renderMultilayer.h"


namespace rend
{

	MultilayerLayer::MultilayerLayer()
		: materialTile(1.0f)
		, microblendTile(1.0f)
		, microblendContrast(1.0f)
		, microblendNormalStrength(1.0f)
		, layerOpacity(1.0f)
		, layerOffset(0.0f, 0.0f)
		, colorScale(0.5f, 0.5f, 0.5f)
		, normalScale(1.0f)
		, roughScaleIn(1.0f)
		, roughBiasIn(0.0f)
		, roughScaleOut(1.0f)
		, roughBiasOut(0.0f)
		, metalScaleIn(1.0f)
		, metalBiasIn(0.0f)
		, metalScaleOut(1.0f)
		, metalBiasOut(0.0f)
		, colorMaskScaleIn(0.0f)
		, colorMaskBiasIn(1.0f)
		, colorMaskScaleOut(0.0f)
		, colorMaskBiasOut(1.0f)
	{
	}

}
