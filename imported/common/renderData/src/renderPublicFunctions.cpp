/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderPublicFunctions.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

namespace GpuWrapApi
{
	red::AbsolutePath GEngineRootPath;

	void SetShadersEnginePath( const red::AbsolutePath& engineRootPath )
	{
		RED_FATAL_ASSERT( GEngineRootPath.Empty(), "Engine path shouldn't be initialized multiple times" );
		GEngineRootPath = engineRootPath;
	}

	const red::AbsolutePath GetShadersEnginePath()
	{
		return GEngineRootPath;
	}
} // end namespace GpuWrapApi

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RED_INLINE Bool IsColorTemperatureActive( Float tmpKelvin )
{
	return tmpKelvin > 0;
}

Vector4 ColorTemperatureToLinearRGB( Float tmpKelvin )
{
	// Get Yuv chromaticity
	// https://en.wikipedia.org/wiki/Planckian_locus
	Float u = ( 0.860117757f + 0.000154118254f * tmpKelvin + 0.000000128641212f * tmpKelvin*tmpKelvin ) / ( 1.0f + 0.000842420235f * tmpKelvin + 0.000000708145163f * tmpKelvin*tmpKelvin );
	Float v = ( 0.317398726f + 0.0000422806245f * tmpKelvin + 0.0000000420481691f * tmpKelvin*tmpKelvin ) / ( 1.0f - 0.0000289741816f * tmpKelvin + 0.000000161456053f * tmpKelvin*tmpKelvin );

	// Convert to CIE Yxy and XYZ
	// https://en.wikipedia.org/wiki/CIE_1931_color_space
	// https://en.wikipedia.org/wiki/CIE_1960_color_space#Relation_to_CIE_XYZ
	Float x = 3 * u / ( 2 * u - 8 * v + 4 );
	Float y = 2 * v / ( 2 * u - 8 * v + 4 );
	Float z = 1 - x - y;
	Float Y = 1.f;
	Float X = Y/y * x;
	Float Z = Y/y * z;

	// XYZ to RGB
	// http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
	Float R =  3.2404542f * X + -1.5371385f * Y + -0.4985314f * Z;
	Float G = -0.9692660f * X +  1.8760108f * Y +  0.0415560f * Z;
	Float B =  0.0556434f * X + -0.2040259f * Y +  1.0572252f * Z;

	// Clamp because sometime values can be slightly below zero
	R = Max( 0.f, R );
	G = Max( 0.f, G );
	B = Max( 0.f, B );

	return Vector4 ( R, G, B, 1.f );
}

Color CalcLightGammaColor( const Color &gammaColor, Float tmpKelvin )
{
	if ( !IsColorTemperatureActive( tmpKelvin ) )
	{
		return gammaColor;
	}

	const Vector4 linearColor = CalcLightLinearColor( gammaColor, tmpKelvin );
	return Color::FromVectorLinearAccu( linearColor );
}

Vector4 CalcLightLinearColor( const Color &gammaColor, Float tmpKelvin )
{
	Vector4 linearColor = gammaColor.ToVectorLinearAccu();

	if ( IsColorTemperatureActive( tmpKelvin ) )
	{
		linearColor *= ColorTemperatureToLinearRGB( tmpKelvin );
	}

	return linearColor;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

Float CalcFovDistanceMultiplier( Float fov )
{
	// assuming that default fov is 75 here
	const Float tanOfDefaultFOV = 0.767326f; // tan( 0.5f * DEG2RAD( 75.f ) );
	const Float fovFactor = tan( DEG2RAD(0.5f * fov) ) / tanOfDefaultFOV;
	const Float ret = fovFactor * fovFactor;
	return Max< Float >( 1.0f, ret );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
