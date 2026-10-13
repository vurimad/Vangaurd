/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderingMemory.h"
#include "renderGradientEntryData.h"

namespace rend
{
	// OBSOLETE!!! USE rend::GradientEntry
	struct RENDER_DATA_API HairProfileGradientEntry : public ISerializable
	{
		RED_USE_MEMORY_POOL(rend::PoolRenderData);
		RTTI_DECLARE_TYPE(HairProfileGradientEntry);
		RED_BASE_CLASS(ISerializable);

		Float	m_value;
		Color	m_color;
	};

	struct HairProfileData
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderData );

		static const Uint32 HairProfile_DefaultSampleCount	= 64;
		static const Uint32 HairProfile_MaxSampleCount		= 128;

		Uint32							sampleCount;
		red::DynArray< GradientEntry >	gradientEntriesID{ rend::PoolRenderData() };
		red::DynArray< GradientEntry >	gradientEntriesRootToTip{ rend::PoolRenderData() };
	
		HairProfileData()
			: sampleCount( HairProfile_DefaultSampleCount )
		{
			{
				gradientEntriesID.Reserve( 2 );

				rend::GradientEntry entryLeft;
				entryLeft.m_value = 0.f;
				entryLeft.m_color = Color( 245, 222, 179 ); // light brown, "wheat"

				rend::GradientEntry entryRight;
				entryRight.m_value = 1.f;
				entryRight.m_color = Color( 139, 69, 19 ); // darker brown, "saddlebrown"

				gradientEntriesID.PushBack( entryLeft );
				gradientEntriesID.PushBack( entryRight );
			}
			{
				gradientEntriesRootToTip.Reserve( 2 );

				rend::GradientEntry entryLeft;
				entryLeft.m_value = 0.f;
				entryLeft.m_color = Color( 51, 51, 51 ); 

				rend::GradientEntry entryRight;
				entryRight.m_value = 1.f;
				entryRight.m_color = Color( 99, 99, 99 );

				gradientEntriesRootToTip.PushBack( entryLeft );
				gradientEntriesRootToTip.PushBack( entryRight );
			}
		}
	
		void Invalidate()
		{
			sampleCount = 0;
			gradientEntriesID.Clear();
			gradientEntriesRootToTip.Clear();
		}
	};
} // rend
