/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderingMemory.h"
#include "renderGradientEntryData.h"

namespace rend
{
	struct GradientData
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderData );

		red::DynArray< GradientEntry > gradientEntries { rend::PoolRenderData() };

		GradientData()
		{
			gradientEntries.Reserve( 2 );

			rend::GradientEntry entryLeft;
			entryLeft.m_value = 0.f;
			entryLeft.m_color = Color::BLACK();

			rend::GradientEntry entryRight;
			entryRight.m_value = 1.f;
			entryRight.m_color = Color::WHITE();

			gradientEntries.PushBack( entryLeft );
			gradientEntries.PushBack( entryRight );
		}

		Vector4 Sample( const Float factor, Bool toGamma = false ) const
		{
			// special cases for empty entries and one entry
			if ( gradientEntries.Empty() )
			{
				return Vector4::ZEROS();
			}

			if ( gradientEntries.Size() == 1 )
			{
				return gradientEntries[ 0 ].m_color.ToVector();
			}

			Uint32 j = 0;
			for ( ; j < gradientEntries.Size() - 1; ++j )
			{
				if ( factor >= gradientEntries[ j ].m_value && factor < gradientEntries[ j + 1 ].m_value )
				{
					break;
				}
			}

			Float scaledFactor = ( factor - gradientEntries[ j ].m_value ) / ( gradientEntries[ j + 1 ].m_value - gradientEntries[ j ].m_value );

			Vector4 linearColorLeft		= Clamp( gradientEntries[j].m_color.ToVectorLinearAccu(), 0.0f, 1.0f );
			Vector4 linearColorRight	= Clamp( gradientEntries[j + 1].m_color.ToVectorLinearAccu(), 0.0f, 1.0f );
			Vector4 linearColor = Vector4::Lerp( linearColorLeft, linearColorRight, scaledFactor );

			return toGamma ? math::ToGammaAccu( linearColor ) : linearColor;
		}

		void Invalidate()
		{
			gradientEntries.Clear();
		}
	};
} // rend
