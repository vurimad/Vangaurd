/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderingMemory.h"

namespace rend
{
	struct SkinProfileData
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderData );

		Float kernelSize;
		Color diffuse;
		Color falloff;
		Float roughness0;
		Float roughness1;
		Float lobeMix;
	
		SkinProfileData()
			: kernelSize( 1.2f )
			, diffuse( Color( 122, 104, 71 ) )
			, falloff( Color( 255,  94, 76 ) )
			, roughness0( 0.75f )
			, roughness1( 1.25f )
			, lobeMix( 0.8f )
		{}
	
		void Invalidate()
		{
			kernelSize = 0.f;
			diffuse = Color::BLACK();
			falloff = Color::BLACK();
			roughness0 = 0.75f;
			roughness1 = 1.25f;
			lobeMix = 0.8f;
		}
	};

	struct RENDER_DATA_API TextureRegionPart : public ISerializable
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderData );

		RTTI_DECLARE_TYPE( TextureRegionPart );
		RED_BASE_CLASS( ISerializable );

		Vector4 m_innerRegion;
		Vector4 m_outerRegion;

		TextureRegionPart()
		{}

		TextureRegionPart( const Vector4& innerRegion, const Vector4& outerRegion )
			: m_innerRegion( innerRegion )
			, m_outerRegion( outerRegion )
		{}
	};

	struct RENDER_DATA_API TextureRegion : public ISerializable
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderData );

		RTTI_DECLARE_TYPE( TextureRegion );
		RED_BASE_CLASS( ISerializable );

		CName										m_name;
		Bool										m_isStretch;
		red::DynArray< rend::TextureRegionPart >	m_regionParts{ rend::PoolRenderData() };
	};

	struct TextureRegionSet
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderData );

		red::DynArray< rend::TextureRegion >	m_regions{ rend::PoolRenderData() };

		Int32 FindRegionIndex( const CName name )
		{
			for ( Int32 i = 0; i < (Int32)m_regions.Size(); ++i )
			{
				if ( m_regions[ i ].m_name == name )
				{
					return i;
				}
			}
			return -1;
		}
	};
} // rend
