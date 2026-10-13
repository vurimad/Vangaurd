/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "curveConverter.h"

#include "mathVector4.h"

namespace CurveDataConverter
{
	TMultiChannelCurve< Float > ToMultiCurve( const TSingleChannelCurve< Vector4 >& singleChannelCurve )
	{
		const Uint32 numberOfChannels = 4;

		TMultiChannelCurve< Float > multiChannelCurve;
		TMultiChannelCurveBuilder<Float> builder( multiChannelCurve );
		builder.SetNumChannels( numberOfChannels );

		for ( Uint32 channel = 0; channel < numberOfChannels; ++channel )
		{
			builder.SetNumKeys( channel, singleChannelCurve.GetNumKeys() );
		}

		builder.Resize();

		for ( Uint32 channel = 0; channel < numberOfChannels; ++channel )
		{
			const Uint32 keysAmount = singleChannelCurve.GetNumKeys();
			for ( Uint32 keyIndex = 0; keyIndex < keysAmount; ++keyIndex )
			{
				builder.SetKeyData( channel, keyIndex, singleChannelCurve.GetKeyTime( keyIndex ), ( *reinterpret_cast<const Vector4*>( singleChannelCurve.GetKeyValue( keyIndex ) ) )[ channel ] );
			}
		}

		return multiChannelCurve;
	}

	TSingleChannelCurve< Vector4 > ToSingleCurve( const TMultiChannelCurve< Float >& multiCurve )
	{
		return TSingleChannelCurve< Vector4 >();
	}
}