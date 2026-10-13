/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderResource.h"

namespace rend
{
	struct RENDER_DATA_API MultilayerLayer
	{
		TRenderPtr<IRenderResource> colorTexture;
		TRenderPtr<IRenderResource> normalTexture;
		TRenderPtr<IRenderResource> roughnessTexture;
		TRenderPtr<IRenderResource> metalnessTexture;
		TRenderPtr<IRenderResource> microblendTexture;

		Float materialTile;
		Float microblendTile;
		Float microblendContrast;
		Float microblendNormalStrength;
		Vector2 microblendOffset;

		Float layerOpacity;
		Vector2 layerOffset;

		Vector3 colorScale;

		Float normalScale;

		Float roughScaleIn;
		Float roughBiasIn;
		Float roughScaleOut;
		Float roughBiasOut;

		Float metalScaleIn;
		Float metalBiasIn;
		Float metalScaleOut;
		Float metalBiasOut;

		Float colorMaskScaleIn;
		Float colorMaskBiasIn;
		Float colorMaskScaleOut;
		Float colorMaskBiasOut;

		MultilayerLayer();
	};

	struct RENDER_DATA_API MultilayerSetupData
	{
		red::ArraySpan<const MultilayerLayer> layers;
		Float ratio;
		Bool useNormal;
		Bool hintAlwaysLoaded;
	};
}
