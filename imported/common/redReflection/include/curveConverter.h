/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "singleChannelCurve.h"
#include "multiChannelCurve.h"

struct Vector4;

namespace CurveDataConverter
{
	extern RED_REFLECTION_API TMultiChannelCurve< Float > ToMultiCurve( const TSingleChannelCurve< Vector4 >& singleCurve );
	extern RED_REFLECTION_API TSingleChannelCurve< Vector4 > ToSingleCurve( const TMultiChannelCurve< Float >& multiCurve );
}