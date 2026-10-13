/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "materialStructures.h"
#include "../../redContainers/include/string/stringBuilder.h"

/// Render stage context
struct SRenderStageContext
{
	RED_STATIC_ASSERT( MVF_Max <= 1 << 8 );
	EMaterialVertexFactory	m_vertexFactory			: 8;
	Bool					m_isDiscarded			: 1;
	Bool					m_isPreSkinned			: 1;
	Bool					m_hasDismemberment		: 1;

	SRenderStageContext()
		: m_vertexFactory( MVF_Max )
		, m_isDiscarded( false )
		, m_isPreSkinned( false )
		, m_hasDismemberment( false )
	{
	}

	SRenderStageContext( EMaterialVertexFactory vertexFactory )
		: m_vertexFactory( vertexFactory )
		, m_isDiscarded( false )
		, m_isPreSkinned( false )
		, m_hasDismemberment( false )
	{
	}

	SRenderStageContext( const SRenderStageContext& other )
		: m_vertexFactory( other.m_vertexFactory )
		, m_isDiscarded( other.m_isDiscarded )
		, m_isPreSkinned( other.m_isPreSkinned )
		, m_hasDismemberment( other.m_hasDismemberment )
	{
	}

	static constexpr Uint32 GetMVFRangeValue( EMaterialVertexFactory mvf )
	{
		return mvf << 3;
	}

	static constexpr Uint32 GetIDRange()
	{
		return GetMVFRangeValue( MVF_Max );
	}

	static Uint32 GetMaxVersionsCount()
	{
		return	MVF_Max *	// number of different vertexFactories
				2 *			// m_isDiscarded can have 2 states
				2 *			// m_isPreSkinned can have 2 states
				2;			// m_hasDismemberment can have 2 states
	}

	Uint32 CalcID() const
	{
		Uint32 val = GetMVFRangeValue( m_vertexFactory );
		val |= m_isPreSkinned		? RED_FLAG( 0 ) : 0;
		val |= m_isDiscarded		? RED_FLAG( 1 ) : 0;
		val |= m_hasDismemberment	? RED_FLAG( 2 ) : 0;

		RED_ASSERT( val < GetIDRange(), "Invalid RenderStageContextID, [Calculated ID: %d][IDRange: %d][VertexFactory: %d][Discarded: %d][PreSkinned: %d][Dismembered: %d]",
			val, GetIDRange(), m_vertexFactory, m_isDiscarded, m_isPreSkinned, m_hasDismemberment );
		return val;
	}

	// Get human readable rendering context description
	red::String ToString() const
	{
		red::StringBuilder< red::String > mode;
		mode.Appendf( "RenderStageContext: [ID: %d, VF: ", CalcID() );

		{
			const rtti::EnumType* vertexFactoryEnumType = GetRttiSystem().FindEnum( TTypeName<EMaterialVertexFactory>::GetTypeName() );
			RED_FATAL_ASSERT( vertexFactoryEnumType );

			CName vertexFactoryName;
			RED_VERIFY( vertexFactoryEnumType->FindName( m_vertexFactory, vertexFactoryName ), "Invalid vertex factory" );

			mode.Append( vertexFactoryName.AsChar() + 4 ); //  +4 to skip "MVF_" prefix
		}

		if ( m_isDiscarded )								mode.Append( "; Discarded" );
		if ( m_isPreSkinned )								mode.Append( "; PreSkinned" );
		if ( m_hasDismemberment )							mode.Append( "; Dismembered" );

		mode.Append( "]" );

		return mode.ToString();
	}

	// Get human readable (short) rendering context description without spaces, so that can be used eg. as filename part
	red::String ToShortString() const
	{
		red::StringBuilder< red::String > mode;
		mode.Appendf( "RSC%d_VF_", CalcID() );

		{
			const rtti::EnumType* vertexFactoryEnumType = GetRttiSystem().FindEnum( TTypeName< EMaterialVertexFactory >::GetTypeName() );
			RED_FATAL_ASSERT( vertexFactoryEnumType );

			CName vertexFactoryName;
			RED_VERIFY( vertexFactoryEnumType->FindName( m_vertexFactory, vertexFactoryName ), "Invalid vertex factory" );

			mode.Append( vertexFactoryName.AsChar() + 4 ); //  +4 to skip "MVF_" prefix
		}

		if ( m_isDiscarded )		mode.Append("_Discarded" );
		if ( m_isPreSkinned )		mode.Append("_PreSkinned" );
		if ( m_hasDismemberment )	mode.Append("_Dismembered" );

		return mode.ToString();
	}
};