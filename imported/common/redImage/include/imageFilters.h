/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

#include "imageFormat.h"
#include "imageAccess.h"

namespace img
{
	namespace filter
	{
		/// Generic box blur filter
		class Box
		{
			RED_FORCE_INLINE static Float Process( ImagePixel& outPixel, const ImagePixel& currentPixel, const Int32 dx, const Int32 dy, const Int32 dz )
			{
				outPixel.AccumulateInplace( currentPixel );
				return 1.0f;
			}
		};

		/// Generic box blur filter limited to 2D
		class Box2D
		{
			RED_FORCE_INLINE static Float Process( ImagePixel& outPixel, const ImagePixel& currentPixel, const Int32 dx, const Int32 dy, const Int32 dz )
			{
				if ( dz != 0 )
					return 0.0f;

				outPixel.AccumulateInplace( currentPixel );
				return 1.0f;
			}
		};

		/// Generic 2D Gaussian blur filter
		/*class Gauss2D
		{
			RED_FORCE_INLINE static Float Process( ImagePixel& outPixel, const ImagePixel& currentPixel, const Int32 dx, const Int32 dy, const Int32 dz )
			{
				// NOT OPTIMAL - it's
				if ( dz != 0 )
					return 0.0f;

				outPixel.AccumulateInplace( currentPixel );
				return 1.0f;
			}
		};*/

	} // filter

} // img