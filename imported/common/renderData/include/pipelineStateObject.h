/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "../../redSystem/include/hash.h"
#include "../../redContainers/include/arraySpan.h"

#include "renderingMemory.h"
#include "vertexLayoutDesc.h"
#include "renderSimpleTypes.h"

//////////////////////////////////////////////////////////////////////

namespace PSODesc
{
	namespace RasterizerMode
	{
		enum FrontFaceWinding : Uint8
		{
			FRONTFACE_CCW,
			FRONTFACE_CW
		};
		enum CullMode : Uint8
		{
			CULL_None,
			CULL_Front,
			CULL_Back
		};
		enum OffsetMode : Uint8
		{
			OFFSET_None,
			OFFSET_NormalBias,
			OFFSET_ShadowBias,
			OFFSET_DecalBias
		};
	}

	namespace BlendMode
	{
		enum WriteMask : Uint8
		{
			MASK_None = 0,
			MASK_R = 1,
			MASK_G = 2,
			MASK_B = 4,
			MASK_A = 8,
			MASK_RG = MASK_R | MASK_G,
			MASK_RB = MASK_R | MASK_B,
			MASK_RA = MASK_R | MASK_A,
			MASK_GB = MASK_G | MASK_B,
			MASK_GA = MASK_G | MASK_A,
			MASK_BA = MASK_B | MASK_A,
			MASK_RGB = MASK_R | MASK_G | MASK_B,
			MASK_RGA = MASK_R | MASK_G | MASK_A,
			MASK_RBA = MASK_R | MASK_B | MASK_A,
			MASK_GBA = MASK_G | MASK_B | MASK_A,
			MASK_RGBA = MASK_R | MASK_G | MASK_B | MASK_A,
		};
		enum Op : Uint8
		{
			OP_Add,
			OP_Subtract,
			OP_RevSub,
			OP_Min,
			OP_Max,
			OP_Or,
			OP_And,
			OP_Xor,
			OP_nAnd,
			OP_nOr,
		};
		enum Factor : Uint8
		{
			FAC_Zero,
			FAC_One,
			FAC_SrcColor,
			FAC_InvSrcColor,
			FAC_SrcAlpha,
			FAC_InvSrcAlpha,
			FAC_DestColor,
			FAC_InvDestColor,
			FAC_DestAlpha,
			FAC_InvDestAlpha,
			FAC_BlendFactor,
			FAC_InvBlendFactor,
			FAC_Src1Color,
			FAC_InvSrc1Color,
			FAC_Src1Alpha,
			FAC_InvSrc1Alpha,
		};
	}
	
	namespace DepthStencilMode
	{
		enum ComparisonMode : Uint8
		{
			COMPARISON_Never,
			COMPARISON_Less,
			COMPARISON_Equal,
			COMPARISON_LessEqual,
			COMPARISON_Greater,
			COMPARISON_NotEqual,
			COMPARISON_GreaterEqual,
			COMPARISON_Always
		};
		enum StencilOpMode : Uint8
		{
			STENCILOP_Keep,
			STENCILOP_Zero,
			STENCILOP_Replace,
			STENCILOP_IncreaseSaturate,
			STENCILOP_DecreaseSaturate,
			STENCILOP_Invert,
			STENCILOP_Increase,
			STENCILOP_Decrease
		};
	}

	///
	enum
	{
		MAX_RENDERTARGETS = 8,
	};

	struct StencilFuncDesc
	{
		RED_USE_MEMORY_POOL( rend::PoolPSO );

#ifdef RTTI_DECLARE_TYPE
		RTTI_DECLARE_TYPE( StencilFuncDesc );
#endif // RTTI_DECLARE_TYPE

		DepthStencilMode::StencilOpMode		m_stencilPassOp;
		DepthStencilMode::ComparisonMode	m_stencilFunc;

		RED_FORCE_INLINE Bool operator==( const StencilFuncDesc& other ) const
		{
			return m_stencilPassOp	== other.m_stencilPassOp
				&& m_stencilFunc	== other.m_stencilFunc;
		}
	};
	static_assert( sizeof( StencilFuncDesc ) == 2, "Unexpected size for StencilFuncDesc" );

	struct RENDER_DATA_API RasterizerModeDesc
	{
		RED_USE_MEMORY_POOL( rend::PoolPSO );

#ifdef RTTI_DECLARE_TYPE
		RTTI_DECLARE_TYPE( RasterizerModeDesc );
#endif // RTTI_DECLARE_TYPE
		//
		// In case of modifying this stuff remember about CalcHash function not to have trash !!!!
		//
		Bool								m_wireframe;
		RasterizerMode::FrontFaceWinding	m_frontWinding;		// winding direction for front faces
		RasterizerMode::CullMode			m_cullMode;			// which faces should be culled
			
		Bool								m_allowMSAA;		// allows MSAA, doesn't necessarily turn it on.
		RasterizerMode::OffsetMode			m_offsetMode;
		Bool								m_scissors;			// required by D3D11 only
		Bool								m_conservativeRasterization;
		Bool								m_valid;
		//
		// In case of modifying this stuff remember about CalcHash function not to have trash !!!!
		//

		RasterizerModeDesc();
		RasterizerModeDesc( Bool wireframe, RasterizerMode::FrontFaceWinding frontWinding, RasterizerMode::CullMode cullMode, Bool allowMSAA, RasterizerMode::OffsetMode offsetMode, Bool enableScissors, Bool conservativeRasterization );

		// #HACK: Different from default constructor. Default constructor gives us CCW face winding and no culling, but
		// material meta loader wants to have CW and back culling as default states. Can't easily change the default
		// constructor because of RTTI diff-based serialization, so this...
		static RasterizerModeDesc Default();

		red::THash64 CalcHash( const red::THash64 baseHash = RED_FNV_OFFSET_BASIS64 ) const
		{
			red::THash64 hash = red::CalculateHash64( &m_wireframe, sizeof( m_wireframe ), baseHash );
			hash = red::CalculateHash64( &m_frontWinding, sizeof( m_frontWinding ), hash );
			hash = red::CalculateHash64( &m_cullMode, sizeof( m_cullMode ), hash );
			hash = red::CalculateHash64( &m_allowMSAA, sizeof( m_allowMSAA ), hash );
			hash = red::CalculateHash64( &m_offsetMode, sizeof( m_offsetMode ), hash );
			hash = red::CalculateHash64( &m_scissors, sizeof( m_scissors ), hash );
			hash = red::CalculateHash64( &m_conservativeRasterization, sizeof( m_conservativeRasterization ), hash );
			hash = red::CalculateHash64( &m_valid, sizeof( m_valid ), hash );
			return hash;
		}

		RED_FORCE_INLINE Bool operator==( const RasterizerModeDesc& other ) const
		{
			return m_wireframe		== other.m_wireframe
				&& m_frontWinding	== other.m_frontWinding
				&& m_cullMode		== other.m_cullMode
				&& m_allowMSAA		== other.m_allowMSAA
				&& m_offsetMode		== other.m_offsetMode
				&& m_scissors		== other.m_scissors
				&& m_conservativeRasterization == other.m_conservativeRasterization
				&& m_valid			== other.m_valid;
		}
	};
	static_assert( sizeof( RasterizerModeDesc ) == 8, "Unexpected size for RasterizerModeDesc" );

	// #TODO: make it smaller (is it doable with the struct in RTTI)
	struct RenderTarget
	{
		RED_USE_MEMORY_POOL( rend::PoolPSO );

#ifdef RTTI_DECLARE_TYPE
		RTTI_DECLARE_TYPE( RenderTarget );
#endif // RTTI_DECLARE_TYPE

		Bool					m_blendEnable;
		BlendMode::WriteMask	m_writeMask;
		BlendMode::Op			m_colorOp;
		BlendMode::Op			m_alphaOp;
		BlendMode::Factor		m_destFactor;
		BlendMode::Factor		m_destAlphaFactor;
		BlendMode::Factor		m_srcFactor;
		BlendMode::Factor		m_srcAlphaFactor;

		RED_FORCE_INLINE Bool operator==( const RenderTarget& other ) const
		{
			return	m_blendEnable == other.m_blendEnable
					&& m_writeMask == other.m_writeMask
					&& m_colorOp == other.m_colorOp
					&& m_alphaOp == other.m_alphaOp
					&& m_destFactor == other.m_destFactor
					&& m_destAlphaFactor == other.m_destAlphaFactor
					&& m_srcFactor == other.m_srcFactor
					&& m_srcAlphaFactor == other.m_srcAlphaFactor;
		}

		RED_FORCE_INLINE Bool operator!=( const RenderTarget& other ) const
		{
			return !operator==( other );
		}

		red::THash64 CalcHash( const red::THash64 baseHash = RED_FNV_OFFSET_BASIS64 ) const
		{
			red::THash64 hash = red::CalculateHash64( &m_blendEnable, sizeof( m_blendEnable ), baseHash );
			hash = red::CalculateHash64( &m_writeMask, sizeof( m_writeMask ), hash );
			hash = red::CalculateHash64( &m_colorOp, sizeof( m_colorOp ), hash );
			hash = red::CalculateHash64( &m_alphaOp, sizeof( m_alphaOp ), hash );
			hash = red::CalculateHash64( &m_destFactor, sizeof( m_destFactor ), hash );
			hash = red::CalculateHash64( &m_destAlphaFactor, sizeof( m_destAlphaFactor ), hash );
			hash = red::CalculateHash64( &m_srcFactor, sizeof( m_srcFactor ), hash );
			hash = red::CalculateHash64( &m_srcAlphaFactor, sizeof( m_srcAlphaFactor ), hash );
			return hash;
		}
	};
	static_assert( sizeof( RenderTarget ) == 8, "Unexpected size for BlendModeDesc::RenderTarget" );

	struct RENDER_DATA_API BlendModeDesc
	{
		RED_USE_MEMORY_POOL( rend::PoolPSO );

#ifdef RTTI_DECLARE_TYPE
		RTTI_DECLARE_TYPE( BlendModeDesc );
#endif // RTTI_DECLARE_TYPE

		Uint8			m_numTargets = 1;	// How many of m_renderTarget are important.
		Bool			m_independent;		// If false, m_renderTarget[0] is used for all targets. If true, first m_numTargets are set according to m_renderTarget[i], rest have no writes
		Bool			m_alphaToCoverage;	// Whether AtoC should be used.
		RenderTarget	m_renderTarget[ MAX_RENDERTARGETS ];

		// Can't have default constructor without making adjustments in gpuApiRenderStateCacheBase.cpp.
		static BlendModeDesc Default();

		red::THash64 CalcHash( const red::THash64 baseHash = RED_FNV_OFFSET_BASIS64 ) const
		{
			red::THash64 values[ MAX_RENDERTARGETS + 1 ];
			values[ MAX_RENDERTARGETS ] = m_numTargets;
			values[ MAX_RENDERTARGETS ] |= m_independent ? (1 << 8) : 0;
			values[ MAX_RENDERTARGETS ] |= m_alphaToCoverage ? (1 << 9) : 0;
			if ( m_independent )
			{
				for ( Uint32 i = 0; i < m_numTargets; ++i )
				{
					values[ i ] = m_renderTarget[ i ].CalcHash( baseHash );
				}
				for ( Uint32 i = m_numTargets; i < MAX_RENDERTARGETS; ++i )
				{
					values[ i ] = 0;
				}
			}
			else
			{
				values[ 0 ] = m_renderTarget[ 0 ].CalcHash( baseHash );
				for ( Uint32 i = 1; i < MAX_RENDERTARGETS; ++i )
				{
					values[ i ] = 0;
				}
			}

			return red::CalculateHash64( values, sizeof( values ), baseHash );
		}

		RED_FORCE_INLINE Bool operator==( const BlendModeDesc& other ) const
		{
			if ( m_numTargets != other.m_numTargets )
			{
				return false;
			}
			if ( m_independent != other.m_independent )
			{
				return false;
			}
			if ( m_alphaToCoverage != other.m_alphaToCoverage )
			{
				return false;
			}
			for ( Uint32 i = 0; i < m_numTargets; ++i )
			{
				if ( m_renderTarget[ i ] != other.m_renderTarget[ i ] )
				{
					return false;
				}
			}
			return true;
		}
	};

	struct RENDER_DATA_API DepthStencilModeDesc
	{
		RED_USE_MEMORY_POOL( rend::PoolPSO );

#ifdef RTTI_DECLARE_TYPE
		RTTI_DECLARE_TYPE( DepthStencilModeDesc );
#endif // RTTI_DECLARE_TYPE

		// depth state
		Bool			m_depthTestEnable;
		Bool			m_depthWriteEnable;
		DepthStencilMode::ComparisonMode	m_depthFunc;
	
		// stencil state
		Bool			m_stencilEnable;
		Bool			m_stencilReadMask;	// whether readmask can be specified when setting mode
		Bool			m_stencilWriteMask;	// whether writemask can be specified when setting mode
		StencilFuncDesc	m_frontFace;

		// Can't have default constructor without making adjustments in gpuApiRenderStateCacheBase.cpp.
		static DepthStencilModeDesc Default();

		red::THash64 CalcHash( const red::THash64 baseHash = RED_FNV_OFFSET_BASIS64 ) const;

		RED_FORCE_INLINE Bool operator==( const DepthStencilModeDesc& other ) const
		{
			return m_depthTestEnable	== other.m_depthTestEnable
				&& m_depthWriteEnable	== other.m_depthWriteEnable
				&& m_depthFunc			== other.m_depthFunc
				&& m_stencilEnable		== other.m_stencilEnable
				&& m_stencilReadMask	== other.m_stencilReadMask
				&& m_stencilWriteMask	== other.m_stencilWriteMask
				&& m_frontFace			== other.m_frontFace;
		}
	};
	static_assert( sizeof( DepthStencilModeDesc ) == 8, "Unexpected size for DepthStencilModeDesc" );



	struct RENDER_DATA_API RenderTargetSetup
	{
		RED_USE_MEMORY_POOL( rend::PoolPSO );

#ifdef RTTI_DECLARE_TYPE
		RTTI_DECLARE_TYPE( RenderTargetSetup );
#endif // RTTI_DECLARE_TYPE

		RED_INLINE RenderTargetSetup()
			: m_dsFormat( GpuWrapApi::TEXFMT_NULL )
			, m_sampleCount( 1 )
		{
		}

		RED_INLINE red::THash64 CalcHash( const red::THash64 baseHash = RED_FNV_OFFSET_BASIS64 ) const
		{
			red::THash64 hash = baseHash;
			hash = red::CalculateHash64( m_rtFormats.Data(), m_rtFormats.DataSize(), hash );
			hash = red::CalculateHash64( &m_dsFormat, sizeof( m_dsFormat ), hash );
			hash = red::CalculateHash64( &m_sampleCount, sizeof( m_sampleCount ), hash );
			return hash;
		}

		RED_INLINE  Bool operator==( const RenderTargetSetup& other ) const
		{
			return m_rtFormats == other.m_rtFormats
				&& m_dsFormat == other.m_dsFormat
				&& m_sampleCount == other.m_sampleCount;
		}
		RED_INLINE  Bool operator!=( const RenderTargetSetup& other ) const
		{
			return !operator==( other );
		}

		red::StaticArray<GpuWrapApi::eTextureFormat, PSODesc::MAX_RENDERTARGETS> m_rtFormats;
		GpuWrapApi::eTextureFormat m_dsFormat;
		Uint8 m_sampleCount;
	};


	enum class PrimitiveTopologyType
	{
		Invalid,
		Point,
		Line,
		Triangle,
		Patch,
	};

	struct RENDER_DATA_API InputLayout
	{
		RED_USE_MEMORY_POOL( rend::PoolPSO );

#ifdef RTTI_DECLARE_TYPE
		RTTI_DECLARE_TYPE( InputLayout );
#endif // RTTI_DECLARE_TYPE

		GpuWrapApi::VertexLayoutDesc m_vertexLayout;
		PrimitiveTopologyType m_primitiveTopology;
	};
}

#ifdef RTTI_DECLARE_TYPE_NAME_IN_NAMESPACE
	RTTI_DECLARE_ENUM_IN_NAMESPACE( FrontFaceWinding,		PSODesc, RasterizerMode );
	RTTI_DECLARE_ENUM_IN_NAMESPACE( CullMode,				PSODesc, RasterizerMode );
	RTTI_DECLARE_ENUM_IN_NAMESPACE( OffsetMode,				PSODesc, RasterizerMode );
	RTTI_DECLARE_ENUM_IN_NAMESPACE( WriteMask,				PSODesc, BlendMode );
	RTTI_DECLARE_ENUM_IN_NAMESPACE( Op,						PSODesc, BlendMode );
	RTTI_DECLARE_ENUM_IN_NAMESPACE( Factor,					PSODesc, BlendMode	);
	RTTI_DECLARE_ENUM_IN_NAMESPACE( ComparisonMode,			PSODesc, DepthStencilMode );
	RTTI_DECLARE_ENUM_IN_NAMESPACE( StencilOpMode,			PSODesc, DepthStencilMode );
	RTTI_DECLARE_ENUM_IN_NAMESPACE( PrimitiveTopologyType,	PSODesc );
#endif // RTTI_DECLARE_TYPE_NAME_IN_NAMESPACE

using namespace PSODesc;

enum ePrimitiveType : Int32
{
	PRIMTYPE_LineList,
	PRIMTYPE_LineStrip,
	PRIMTYPE_TriangleList,
	PRIMTYPE_TriangleStrip,
	PRIMTYPE_PointList,
	PRIMTYPE_QuadList,
	PRIMTYPE_1CP_PATCHLIST,
	PRIMTYPE_3CP_PATCHLIST,
	PRIMTYPE_4CP_PATCHLIST,
	PRIMTYPE_Invalid,

	// Remember to keep this one uptodate
	PRIMTYPE_FIRST_CP_PATCH = PRIMTYPE_1CP_PATCHLIST,
};

// Output-Merger State
struct RENDER_DATA_API SOMState
{
	RED_USE_MEMORY_POOL( rend::PoolPSO );

#ifdef RTTI_DECLARE_TYPE
	RTTI_DECLARE_TYPE( SOMState );
#endif // RTTI_DECLARE_TYPE

	RED_INLINE SOMState()
		: m_stencilReadMask(0)
		, m_stencilWriteMask(0)
		, m_stencilRef(0)
	{
		m_depthStencilModeDesc = DepthStencilModeDesc::Default();
		m_rasterizerModeDesc = RasterizerModeDesc::Default();
		m_blendModeDesc = BlendModeDesc::Default();
	}

	RED_INLINE red::THash64 CalcHash( const red::THash64 baseHash = RED_FNV_OFFSET_BASIS64 ) const
	{
		red::THash64 hash = baseHash;
		hash = m_depthStencilModeDesc.CalcHash( hash );
		hash = m_rasterizerModeDesc.CalcHash( hash );
		hash = m_blendModeDesc.CalcHash( hash );
		hash = red::CalculateHash64( &m_stencilReadMask, sizeof( m_stencilReadMask ), hash );
		hash = red::CalculateHash64( &m_stencilWriteMask, sizeof( m_stencilWriteMask ), hash );
		hash = red::CalculateHash64( &m_stencilRef, sizeof( m_stencilRef ), hash );
		return hash;
	}

	RED_FORCE_INLINE Bool operator==( const SOMState& other ) const
	{
		return m_depthStencilModeDesc	== other.m_depthStencilModeDesc
			&& m_rasterizerModeDesc		== other.m_rasterizerModeDesc
			&& m_blendModeDesc			== other.m_blendModeDesc
			&& m_stencilReadMask		== other.m_stencilReadMask
			&& m_stencilWriteMask		== other.m_stencilWriteMask
			&& m_stencilRef				== other.m_stencilRef;
	}
	RED_FORCE_INLINE Bool operator!=( const SOMState& other ) const
	{
		return !operator==( other );
	}

	DepthStencilModeDesc	m_depthStencilModeDesc;
	RasterizerModeDesc		m_rasterizerModeDesc;
	BlendModeDesc			m_blendModeDesc;
	Uint8					m_stencilReadMask;
	Uint8					m_stencilWriteMask;
	Uint8					m_stencilRef; // todo: this shouldn't really be here (should be set in runtime)
};
