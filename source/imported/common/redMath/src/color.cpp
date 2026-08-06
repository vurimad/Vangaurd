/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "color.h"

namespace math
{

	GammaToLinearLUT g_gammaToLinearLUT;

	GammaToLinearLUT::GammaToLinearLUT()
	{
		// generate LUT
		for ( Uint32 i = 0; i < Size; ++i )
		{
			const Float gammaSpace = (Float)i / (Float)( Size - 1 );
			const Float linearSpace = ToLinearAccu( gammaSpace );

			floatLut[ i ] = linearSpace;
			halfLut[ i ] = Half( linearSpace );
		}
	}

} // math
