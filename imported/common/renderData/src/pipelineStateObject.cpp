/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "pipelineStateObject.h"
#include "materialDataProvider.h"

red::THash64 DepthStencilModeDesc::CalcHash( const red::THash64 baseHash /*=RED_FNV_OFFSET_BASIS64*/ ) const
{
	Uint64 value = 0;

#define ADD_VAL_BITS(val,num)		do { value <<= num; value |= val; } while (0)
#define ADD_BOOL_BITS(val)			do { value <<= 1; value |= (val?1:0); } while (0)
		
	ADD_BOOL_BITS( m_depthTestEnable );
	ADD_BOOL_BITS( m_depthWriteEnable );
	ADD_VAL_BITS( m_depthFunc, 3 );
	ADD_BOOL_BITS( m_stencilEnable );
	ADD_BOOL_BITS( m_stencilReadMask );
	ADD_BOOL_BITS( m_stencilWriteMask );
	ADD_VAL_BITS( m_frontFace.m_stencilPassOp, 4 );
	ADD_VAL_BITS( m_frontFace.m_stencilFunc, 4 );

#undef ADD_VAL_BITS
#undef ADD_BOOL_BITS

	return red::CalculateHash64( &value, sizeof(value), baseHash );
}

RTTI_BEGIN_ENUM_IN_NAMESPACE( ComparisonMode, PSODesc, DepthStencilMode );
	RTTI_ENUM_OPTION( COMPARISON_Never );
	RTTI_ENUM_OPTION( COMPARISON_Less );
	RTTI_ENUM_OPTION( COMPARISON_Equal );
	RTTI_ENUM_OPTION( COMPARISON_LessEqual );
	RTTI_ENUM_OPTION( COMPARISON_Greater );
	RTTI_ENUM_OPTION( COMPARISON_NotEqual );
	RTTI_ENUM_OPTION( COMPARISON_GreaterEqual );
	RTTI_ENUM_OPTION( COMPARISON_Always );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( StencilOpMode, PSODesc, DepthStencilMode );
	RTTI_ENUM_OPTION( STENCILOP_Keep );
	RTTI_ENUM_OPTION( STENCILOP_Zero );
	RTTI_ENUM_OPTION( STENCILOP_Replace );
	RTTI_ENUM_OPTION( STENCILOP_IncreaseSaturate );
	RTTI_ENUM_OPTION( STENCILOP_DecreaseSaturate );
	RTTI_ENUM_OPTION( STENCILOP_Invert );
	RTTI_ENUM_OPTION( STENCILOP_Increase );
	RTTI_ENUM_OPTION( STENCILOP_Decrease );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( FrontFaceWinding, PSODesc, RasterizerMode );
	RTTI_ENUM_OPTION( FRONTFACE_CCW );
	RTTI_ENUM_OPTION( FRONTFACE_CW );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( CullMode, PSODesc, RasterizerMode );
	RTTI_ENUM_OPTION( CULL_None );
	RTTI_ENUM_OPTION( CULL_Front );
	RTTI_ENUM_OPTION( CULL_Back );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( OffsetMode, PSODesc, RasterizerMode );
	RTTI_ENUM_OPTION( OFFSET_None );
	RTTI_ENUM_OPTION( OFFSET_NormalBias );
	RTTI_ENUM_OPTION( OFFSET_ShadowBias );
	RTTI_ENUM_OPTION( OFFSET_DecalBias );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( WriteMask, PSODesc, BlendMode );
	RTTI_ENUM_OPTION( MASK_None );
	RTTI_ENUM_OPTION( MASK_R );
	RTTI_ENUM_OPTION( MASK_G );
	RTTI_ENUM_OPTION( MASK_B );
	RTTI_ENUM_OPTION( MASK_A );
	RTTI_ENUM_OPTION( MASK_RG );
	RTTI_ENUM_OPTION( MASK_RB );
	RTTI_ENUM_OPTION( MASK_RA );
	RTTI_ENUM_OPTION( MASK_GB );
	RTTI_ENUM_OPTION( MASK_GA );
	RTTI_ENUM_OPTION( MASK_BA );
	RTTI_ENUM_OPTION( MASK_RGB );
	RTTI_ENUM_OPTION( MASK_RGA );
	RTTI_ENUM_OPTION( MASK_RBA );
	RTTI_ENUM_OPTION( MASK_GBA );
	RTTI_ENUM_OPTION( MASK_RGBA );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( Op, PSODesc, BlendMode );
{
	RTTI_ENUM_OPTION( OP_Add );
	RTTI_ENUM_OPTION( OP_Subtract );
	RTTI_ENUM_OPTION( OP_RevSub );
	RTTI_ENUM_OPTION( OP_Min );
	RTTI_ENUM_OPTION( OP_Max );
	RTTI_ENUM_OPTION( OP_Or );
	RTTI_ENUM_OPTION( OP_And );
	RTTI_ENUM_OPTION( OP_Xor );
	RTTI_ENUM_OPTION( OP_nOr );
	RTTI_ENUM_OPTION( OP_nAnd );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( Factor, PSODesc, BlendMode );
	RTTI_ENUM_OPTION( FAC_Zero );
	RTTI_ENUM_OPTION( FAC_One );
	RTTI_ENUM_OPTION( FAC_SrcColor );
	RTTI_ENUM_OPTION( FAC_InvSrcColor );
	RTTI_ENUM_OPTION( FAC_SrcAlpha );
	RTTI_ENUM_OPTION( FAC_InvSrcAlpha );
	RTTI_ENUM_OPTION( FAC_DestColor );
	RTTI_ENUM_OPTION( FAC_InvDestColor );
	RTTI_ENUM_OPTION( FAC_DestAlpha );
	RTTI_ENUM_OPTION( FAC_InvDestAlpha );
	RTTI_ENUM_OPTION( FAC_BlendFactor );
	RTTI_ENUM_OPTION( FAC_InvBlendFactor );
	RTTI_ENUM_OPTION( FAC_Src1Color );
	RTTI_ENUM_OPTION( FAC_InvSrc1Color );
	RTTI_ENUM_OPTION( FAC_Src1Alpha );
	RTTI_ENUM_OPTION( FAC_InvSrc1Alpha );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( PrimitiveTopologyType, PSODesc );
{
	RTTI_ENUM_OPTION( Invalid );
	RTTI_ENUM_OPTION( Point );
	RTTI_ENUM_OPTION( Line );
	RTTI_ENUM_OPTION( Triangle );
	RTTI_ENUM_OPTION( Patch );
}
RTTI_END_ENUM();


//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE_IN_NAMESPACE( StencilFuncDesc, PSODesc );
	RTTI_PROPERTY( m_stencilPassOp ).readOnly();
	RTTI_PROPERTY( m_stencilFunc ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( DepthStencilModeDesc, PSODesc );
	RTTI_PROPERTY( m_depthTestEnable ).readOnly();
	RTTI_PROPERTY( m_depthWriteEnable ).readOnly();
	RTTI_PROPERTY( m_depthFunc ).readOnly();
	RTTI_PROPERTY( m_stencilEnable ).readOnly();
	RTTI_PROPERTY( m_stencilReadMask ).readOnly();
	RTTI_PROPERTY( m_stencilWriteMask ).readOnly();
	RTTI_PROPERTY( m_frontFace ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RasterizerModeDesc, PSODesc );
	RTTI_PROPERTY( m_wireframe ).readOnly();
	RTTI_PROPERTY( m_frontWinding ).readOnly();
	RTTI_PROPERTY( m_cullMode ).readOnly();
	RTTI_PROPERTY( m_allowMSAA ).readOnly();
	RTTI_PROPERTY( m_conservativeRasterization ).readOnly();
	RTTI_PROPERTY( m_offsetMode ).readOnly();
	RTTI_PROPERTY( m_scissors ).readOnly();
	RTTI_PROPERTY( m_valid ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTarget, PSODesc );
	RTTI_PROPERTY( m_blendEnable ).readOnly();
	RTTI_PROPERTY( m_writeMask ).readOnly();
	RTTI_PROPERTY( m_colorOp ).readOnly();
	RTTI_PROPERTY( m_alphaOp ).readOnly();
	RTTI_PROPERTY( m_destFactor ).readOnly();
	RTTI_PROPERTY( m_destAlphaFactor ).readOnly();
	RTTI_PROPERTY( m_srcFactor ).readOnly();
	RTTI_PROPERTY( m_srcAlphaFactor ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( BlendModeDesc, PSODesc );
	RTTI_PROPERTY( m_numTargets ).readOnly();
	RTTI_PROPERTY( m_independent ).readOnly();
	RTTI_PROPERTY( m_alphaToCoverage ).readOnly();
	RTTI_PROPERTY( m_renderTarget ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTargetSetup, PSODesc )
{
	RTTI_PROPERTY( m_rtFormats ).readOnly();
	RTTI_PROPERTY( m_dsFormat ).readOnly();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( SOMState );
{
	RTTI_PROPERTY( m_depthStencilModeDesc ).readOnly();
	RTTI_PROPERTY( m_rasterizerModeDesc ).readOnly();
	RTTI_PROPERTY( m_blendModeDesc ).readOnly();
	RTTI_PROPERTY( m_stencilReadMask ).readOnly();
	RTTI_PROPERTY( m_stencilWriteMask ).readOnly();
	RTTI_PROPERTY( m_stencilRef ).readOnly();
}
RTTI_END_TYPE();


//////////////////////////////////////////////////////////////////////////

static_assert(IMaterialDataProviderDesc::PT_MAX == 17, "EParameterType changed, check here!");
RTTI_BEGIN_ENUM_IN_NAMESPACE( EParameterType, IMaterialDataProviderDesc );
	RTTI_ENUM_OPTION( PT_None );
	RTTI_ENUM_OPTION( PT_Texture );
	RTTI_ENUM_OPTION( PT_Color );
	RTTI_ENUM_OPTION( PT_Cube );
	RTTI_ENUM_OPTION( PT_Vector );
	RTTI_ENUM_OPTION( PT_Scalar );
	RTTI_ENUM_OPTION( PT_Bool );
	RTTI_ENUM_OPTION( PT_TextureArray );
	RTTI_ENUM_OPTION( PT_StructBuffer );
	RTTI_ENUM_OPTION( PT_Cpu_NameU64 );
	RTTI_ENUM_OPTION( PT_SkinProfile );
	RTTI_ENUM_OPTION( PT_MultilayerSetup );
	RTTI_ENUM_OPTION( PT_MultilayerMask );
	RTTI_ENUM_OPTION( PT_HairProfile );
	RTTI_ENUM_OPTION( PT_FoliageProfile );
	RTTI_ENUM_OPTION( PT_TerrainSetup );
	RTTI_ENUM_OPTION( PT_Gradient );
	RTTI_ENUM_OPTION( PT_MAX );
	RTTI_END_ENUM();

//////////////////////////////////////////////////////////////////////////


PSODesc::RasterizerModeDesc::RasterizerModeDesc()
// #NOTE: DO NOT CHANGE THESE! RasterizerModeDesc can be serialized through RTTI, which relies on default object diffs.
// So if you change this, things can be loaded incorrectly.
	: m_wireframe( false )
	, m_frontWinding( RasterizerMode::FRONTFACE_CCW )
	, m_cullMode( RasterizerMode::CULL_None )
	, m_allowMSAA( false )
	, m_offsetMode( RasterizerMode::OFFSET_None )
	, m_scissors( false )
	, m_conservativeRasterization( false )
	, m_valid( false )
{
}

PSODesc::RasterizerModeDesc::RasterizerModeDesc( Bool wireframe, RasterizerMode::FrontFaceWinding frontWinding, RasterizerMode::CullMode cullMode, Bool allowMSAA, RasterizerMode::OffsetMode offsetMode, Bool enableScissors, Bool conservativeRasterization )
	: m_wireframe( wireframe )
	, m_frontWinding( frontWinding )
	, m_cullMode( cullMode )
	, m_allowMSAA( allowMSAA )
	, m_offsetMode( offsetMode )
	, m_scissors( enableScissors )
	, m_conservativeRasterization( conservativeRasterization )
	, m_valid( true )
{}


PSODesc::RasterizerModeDesc PSODesc::RasterizerModeDesc::Default()
{
	RasterizerModeDesc result;
	red::Memzero( &result, sizeof( result ) );
	result.m_wireframe		= false;
	result.m_allowMSAA		= false;
	result.m_conservativeRasterization = false;
	result.m_frontWinding	= RasterizerMode::FRONTFACE_CW;
	result.m_cullMode		= RasterizerMode::CULL_Back;
	result.m_offsetMode		= RasterizerMode::OFFSET_None;
	return result;
}


PSODesc::BlendModeDesc PSODesc::BlendModeDesc::Default()
{
	BlendModeDesc result = {};
	result.m_independent						= false;
	result.m_alphaToCoverage					= false;
	result.m_numTargets							= 1;
	RenderTarget& defaultRenderTargetDesc		= result.m_renderTarget[0];
	defaultRenderTargetDesc.m_blendEnable		= false;
	defaultRenderTargetDesc.m_writeMask			= BlendMode::MASK_RGBA;
	defaultRenderTargetDesc.m_colorOp			= BlendMode::OP_Add;
	defaultRenderTargetDesc.m_alphaOp			= BlendMode::OP_Add;
	defaultRenderTargetDesc.m_srcFactor			= BlendMode::FAC_SrcColor;
	defaultRenderTargetDesc.m_destFactor		= BlendMode::FAC_Zero;
	defaultRenderTargetDesc.m_srcAlphaFactor	= BlendMode::FAC_SrcAlpha;
	defaultRenderTargetDesc.m_destAlphaFactor	= BlendMode::FAC_Zero;
	return result;
}


PSODesc::DepthStencilModeDesc PSODesc::DepthStencilModeDesc::Default()
{
	DepthStencilModeDesc result = {};
	result.m_depthTestEnable			= true;
	result.m_depthWriteEnable			= true;
	result.m_stencilEnable				= true;
	result.m_depthFunc					= DepthStencilMode::COMPARISON_LessEqual;
	result.m_stencilReadMask			= false;
	result.m_stencilWriteMask			= false;
	result.m_frontFace.m_stencilFunc	= DepthStencilMode::COMPARISON_LessEqual;
	result.m_frontFace.m_stencilPassOp	= DepthStencilMode::STENCILOP_Keep;
	return result;
}
