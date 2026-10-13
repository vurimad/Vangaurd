/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once

//---------------------------------------------------------------

/// Mesh chunk shadow cascade masks
/// LEFT ONLY FOR CONVERTING OLD ASSETS!!!!
enum EMeshChunkRenderMask : Uint16
{
	MCR_Scene				= RED_FLAG(0),
	MCR_Cascade1			= RED_FLAG(1),		//< Used when cascades heuristic is off.
	MCR_Cascade2			= RED_FLAG(2),		//< Used when cascades heuristic is off.
	MCR_Cascade3			= RED_FLAG(3),		//< Used when cascades heuristic is off.
	MCR_Cascade4			= RED_FLAG(4),		//< Used when cascades heuristic is off.
	MCR_LocalShadows		= RED_FLAG(5),
	MCR_IsTwoSided			= RED_FLAG(6),
	MCR_DistantShadows		= RED_FLAG(7),
	MCR_IsRayTracedEmissive = RED_FLAG(8),
 	MCR_IsGIVisible			= RED_FLAG(9),
 	MCR_IsGITransparent		= RED_FLAG(10),
 	MCR_PrefabProxy			= RED_FLAG(11),
	MCR_Cascades			= RED_FLAG(12)		//< Used when cascades heuristic is on.
};

RTTI_DECLARE_BITFIELD( EMeshChunkRenderMask );

//---------------------------------------------------------------

/// Special properties of a mesh chunk
enum EMeshChunkFlags : Uint16
{
	MCF_RenderInScene		= RED_FLAG( 0 ),
	MCF_RenderInShadows		= RED_FLAG( 1 ),
	MCF_IsTwoSided			= RED_FLAG( 2 ),
	MCF_IsRayTracedEmissive = RED_FLAG( 3 ),
	MCF_IsGIVisible			= RED_FLAG( 4 ),
	MCF_IsGITransparent		= RED_FLAG( 5 ),
	MCF_IsPrefabProxy		= RED_FLAG( 6 ),
	MCF_RenderInRainMap		= RED_FLAG( 7 ),
	MCF_CollectInRayTraceBVH	= RED_FLAG( 8 )
};

RTTI_DECLARE_BITFIELD( EMeshChunkFlags );

//---------------------------------------------------------------

/// Bias on mesh shadow rendering distance
enum EMeshShadowImportanceBias : Int32
{
	MSIB_EvenLessImportant=-2,
	MSIB_LessImportant=-1,
	MSIB_Default=0,
	MSIB_MoreImportant=1,
	MSIB_EvenMoreImportant=2,
};

RTTI_DECLARE_ENUM( EMeshShadowImportanceBias );

//---------------------------------------------------------------

// special mesh debug color which gets interpreted as "color based on LOD" in the renderer
static const math::Color c_meshDebugColoringMode_LOD = math::Color( 0, 0, 1, 0 );

// A set of definitive flags, determining where a given mesh proxy will render to
// (after considering node/component flags, mesh chunk flags, heuristic, and whatever else we will come up with)
// If "X" is set, this proxy renders to "X"
union ShadowFlags
{
	struct
	{
		Bool m_cascade0 : 1;		//!< Renders to first cascade?
		Bool m_cascade1 : 1;		//!< Renders to second cascade?
		Bool m_cascade2 : 1;		//!< Renders to third cascade?
		Bool m_cascade3 : 1;		//!< Renders to fourth cascade?
		Bool m_distantCascade0 : 1;	//!< Renders to first distant shadows cascade?
		Bool m_distantCascade1 : 1;	//!< Renders to second distant shadows cascade?
		Bool m_distantCascade2 : 1;	//!< Renders to remaining distant shadows cascades?
		Bool m_localShadows : 1;	//!< Renders to local shadows?
	};
	Uint8 m_mask = 0;				//!< a view for handy masking

	// Make it easy to check whether any cascade is enabled
	static Uint8 CASCADE_SHADOWS_MASK() { return 0x0F; }
	static Uint8 DISTANT_SHADOWS_MASK() { return 0x70; }
	static Uint8 GLOBAL_SHADOWS_MASK() { return CASCADE_SHADOWS_MASK() | DISTANT_SHADOWS_MASK(); }
};

static_assert( sizeof( ShadowFlags ) == 1, "Invalid shadow mask size, don't make it bigger" );
