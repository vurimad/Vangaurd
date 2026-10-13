/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"

#include "image.h"
#include "imageIterator.h"
#include "imageAccess.h"
#include "imageUtils.h"

namespace img
{
	namespace utils
	{
		REDIMAGE_API void StretchHistogram( ImagePtr& source, math::Vector3& outHistBiasMulCoef, math::Vector3& outHistBiasAddCoef )
		{
			RED_FATAL_ASSERT( source != nullptr, "Invalid source image for downscaling" );

			math::Vector4 histogramMin(1,1,1,1);
			math::Vector4 histogramMax(0,0,0,0);

			ImageAccess src( *source );

			Bool isGamma = src.IsInGammaSpace();
			ImageIterator it = source->GetIterator();
			auto funcMinMax = [&histogramMin, &histogramMax, isGamma](ImageIterator& it, ImagePixel& pixel)->Bool 
			{ 
				RED_UNUSED( isGamma );
				//TODO: Implement simd Min Max
				histogramMin = math::Vector4( math::Min(histogramMin.X, pixel[0]), math::Min(histogramMin.Y, pixel[1]), math::Min(histogramMin.Z, pixel[2]), 0.0f );
				histogramMax = math::Vector4( math::Max(histogramMax.X, pixel[0]), math::Max(histogramMax.Y, pixel[1]), math::Max(histogramMax.Z, pixel[2]), 0.0f );
				return true;
			};
			img::utils::ProcessImage( src, it, funcMinMax );

			// Validate data
			math::Vector4::Clamp4(histogramMin, 0.0f, 1.0f);
			math::Vector4::Clamp4(histogramMax, 0.0f, 1.0f);

			math::Vector4 histogramMinNegative = math::Vector4( -histogramMin.X, -histogramMin.Y, -histogramMin.Z, 0.0f );
			math::Vector4 biasDenominator = math::Vector4( 1.0f, 1.0f, 1.0f, 1.0f ) / (histogramMax - histogramMin);
			biasDenominator = math::Vector4( biasDenominator.X, biasDenominator.Y, biasDenominator.Z, 1.0f );

			// Stretch histogram
			ImageIterator it1 = source->GetIterator();
			auto funcStretch = [histogramMinNegative, biasDenominator](ImageIterator& it, ImagePixel& pixel)->Bool 
			{ 
				ImagePixel a( &histogramMinNegative.X );
				pixel.AccumulateInplace( a, 1.0f );
				ImagePixel b( &biasDenominator.X );
				pixel.ModulateInplace( b );
				return true;
			};
			img::utils::ProcessImageInplace( src, it1, funcStretch );

			outHistBiasMulCoef = histogramMax - histogramMin;
			outHistBiasAddCoef = histogramMin;
		}

	} // utils

} // img
