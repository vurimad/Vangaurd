/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/// Calculates linear color for given temperature
RENDER_DATA_API Vector4 ColorTemperatureToLinearRGB( Float tmpKelvin );

/// Calculates linear color for given params. Note that the values can go beyond 1 of temperature!
RENDER_DATA_API Vector4 CalcLightLinearColor( const Color &color, Float tmpKelvin );

/// Calculates gamma color for given params. Note that the color value may get clamped.
RENDER_DATA_API Color CalcLightGammaColor( const Color &color, Float tmpKelvin );

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

namespace GpuWrapApi
{
	/// Set shaders engine path
	RENDER_DATA_API void SetShadersEnginePath( const red::AbsolutePath& engineRootPath );

	/// Get shaders engine path
	RENDER_DATA_API const red::AbsolutePath GetShadersEnginePath();
} // end namespace GpuWrapApi

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RENDER_DATA_API Float CalcFovDistanceMultiplier( Float fov );

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
