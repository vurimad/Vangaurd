/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderDataApi.h"

namespace rend
{
	class DebugFilterMask;

	
	enum RenderFilterPreset : Uint8
	{
		Game = RED_FLAG( 0 ),
		User = RED_FLAG( 1 ),
		Count = 2,
		All = std::numeric_limits< Uint8 >::max()
	};

	RENDER_DATA_API void InitializeRenderFilterPresets();
	RENDER_DATA_API void ApplyRenderFilterMask( RenderFilterPreset preset, DebugFilterMask& filterMask );
	RENDER_DATA_API RenderFilterPreset GetRenderFilterPreset( red::StringView value );
	RENDER_DATA_API red::ArraySpan< const red::StringView > GetRenderFilterPresetNames();
	RENDER_DATA_API red::StringView GetRenderFilterPresetName( RenderFilterPreset preset );

	RENDER_DATA_API void SetGameRenderPreset( RenderFilterPreset preset );
	RENDER_DATA_API RenderFilterPreset GetGameRenderPreset();

} // rend
