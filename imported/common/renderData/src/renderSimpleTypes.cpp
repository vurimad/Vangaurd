/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderSimpleTypes.h"
#include "renderPublicEnums.h"

static_assert( GpuWrapApi::TEXG_MAX == 34, "Add new entries" );
static_assert( GpuWrapApi::TEXG_Generic_End - GpuWrapApi::TEXG_Generic_Begin == 7, "Add missing TEX_GENERIC groups" );
static_assert( GpuWrapApi::TEXG_Multilayer_End - GpuWrapApi::TEXG_Multilayer_Begin == 4, "Add missing TEX_MULTILAYER groups" );
RTTI_BEGIN_ENUM_IN_NAMESPACE( eTextureGroup, GpuWrapApi );
{
	RTTI_ENUM_OPTION( TEXG_Generic_Color );
	RTTI_ENUM_OPTION( TEXG_Generic_Grayscale );
	RTTI_ENUM_OPTION( TEXG_Generic_Normal );
	RTTI_ENUM_OPTION( TEXG_Generic_Data );
	RTTI_ENUM_OPTION( TEXG_Generic_UI );
	RTTI_ENUM_OPTION( TEXG_Generic_Font );
	RTTI_ENUM_OPTION( TEXG_Generic_LUT );
	RTTI_ENUM_OPTION( TEXG_Generic_MorphBlend );
	RTTI_ENUM_OPTION( TEXG_Multilayer_Color );
	RTTI_ENUM_OPTION( TEXG_Multilayer_Normal );
	RTTI_ENUM_OPTION( TEXG_Multilayer_Grayscale );
	RTTI_ENUM_OPTION( TEXG_Multilayer_Microblend );
	//RTTI_ENUM_OPTION( TEXG_Multilayer_MLMask ); // #TODO: Even though int's in TEXG_Multilayer_* set, this is not RTTI accessible.
}
RTTI_END_ENUM();


RTTI_BEGIN_ENUM_IN_NAMESPACE( eTextureFormat, GpuWrapApi );
{
	RTTI_ENUM_OPTION( TEXFMT_A8_Unorm ).legacyName( "TEXFMT_A8" );
	RTTI_ENUM_OPTION( TEXFMT_R8_Unorm ).legacyName( "TEXFMT_R8" );
	RTTI_ENUM_OPTION( TEXFMT_L8_Unorm ).legacyName( "TEXFMT_L8" );
	RTTI_ENUM_OPTION( TEXFMT_R8G8_Unorm ).legacyName( "TEXFMT_R8G8" );
	RTTI_ENUM_OPTION( TEXFMT_R8G8B8X8_Unorm ).legacyName( "TEXFMT_R8G8B8X8" );
	RTTI_ENUM_OPTION( TEXFMT_R8G8B8A8_Unorm ).legacyName( "TEXFMT_R8G8B8A8" );
	RTTI_ENUM_OPTION( TEXFMT_R8G8B8A8_Unorm_SRGB ).legacyName( "TEXFMT_R8G8B8A8" );
	RTTI_ENUM_OPTION( TEXFMT_R8G8B8A8_Snorm );
	RTTI_ENUM_OPTION( TEXFMT_R16_Unorm ).legacyName( "TEXFMT_Uint_16_norm" );
	RTTI_ENUM_OPTION( TEXFMT_R16_Uint ).legacyName( "TEXFMT_Uint_16" );
	RTTI_ENUM_OPTION( TEXFMT_R32_Uint ).legacyName( "TEXFMT_Uint_32" );
	RTTI_ENUM_OPTION( TEXFMT_R32G32B32A32_Uint ).legacyName( "TEXFMT_Uint_R32G32B32A32" );
	RTTI_ENUM_OPTION( TEXFMT_R32G32_Uint );
	RTTI_ENUM_OPTION( TEXFMT_R16G16B16A16_Unorm );
	RTTI_ENUM_OPTION( TEXFMT_R16G16B16A16_Uint ).legacyName( "TEXFMT_Uint_R32G32B32A32" );
	RTTI_ENUM_OPTION( TEXFMT_R16G16_Uint );
	RTTI_ENUM_OPTION( TEXFMT_R10G10B10A2_Unorm ).legacyName( "TEXFMT_R10G10B10A2" );
	RTTI_ENUM_OPTION( TEXFMT_R16G16B16A16_Float ).legacyName( "TEXFMT_Float_R16G16B16A16" );
	RTTI_ENUM_OPTION( TEXFMT_R11G11B10_Float ).legacyName( "TEXFMT_Float_R11G11B10" );
	RTTI_ENUM_OPTION( TEXFMT_R16G16_Float ).legacyName( "TEXFMT_Float_R16G16" );
	RTTI_ENUM_OPTION( TEXFMT_R32G32_Float ).legacyName( "TEXFMT_Float_R32G32" );
	RTTI_ENUM_OPTION( TEXFMT_R32G32B32A32_Float ).legacyName( "TEXFMT_Float_R32G32B32A32" );
	RTTI_ENUM_OPTION( TEXFMT_R32_Float ).legacyName( "TEXFMT_Float_R32" );
	RTTI_ENUM_OPTION( TEXFMT_R16_Float ).legacyName( "TEXFMT_Float_R16" );
	RTTI_ENUM_OPTION( TEXFMT_D24S8 );
	RTTI_ENUM_OPTION( TEXFMT_D32FS8 );
	RTTI_ENUM_OPTION( TEXFMT_D32F );
	RTTI_ENUM_OPTION( TEXFMT_D16U );
	RTTI_ENUM_OPTION( TEXFMT_BC1 );
	RTTI_ENUM_OPTION( TEXFMT_BC1_SRGB );
	RTTI_ENUM_OPTION( TEXFMT_BC2 );
	RTTI_ENUM_OPTION( TEXFMT_BC2_SRGB );
	RTTI_ENUM_OPTION( TEXFMT_BC3 );
	RTTI_ENUM_OPTION( TEXFMT_BC3_SRGB );
	RTTI_ENUM_OPTION( TEXFMT_BC4 );
	RTTI_ENUM_OPTION( TEXFMT_BC5 );
	RTTI_ENUM_OPTION( TEXFMT_BC6H_UNSIGNED ).legacyName("TEXFMT_BC6H");
	RTTI_ENUM_OPTION( TEXFMT_BC6H_SIGNED );
	RTTI_ENUM_OPTION( TEXFMT_BC7 );
	RTTI_ENUM_OPTION( TEXFMT_BC7_SRGB );
	RTTI_ENUM_OPTION( TEXFMT_R8_Uint );
	RTTI_ENUM_OPTION( TEXFMT_R16G16_Unorm );
	RTTI_ENUM_OPTION( TEXFMT_R16G16_Sint );
	RTTI_ENUM_OPTION( TEXFMT_R16G16_Snorm );
	RTTI_ENUM_OPTION( TEXFMT_B5G6R5_Unorm );
#ifdef RED_PLATFORM_DURANGO
	RTTI_ENUM_OPTION( TEXFMT_R10G10B10_6E4_A2_Float ).legacyName( "TEXFMT_R10G10B10_6E4_A2_FLOAT" );
#endif
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE(eTextureType, GpuWrapApi);
{
	RTTI_ENUM_OPTION( TEXTYPE_2D );
	RTTI_ENUM_OPTION( TEXTYPE_CUBE );
	RTTI_ENUM_OPTION( TEXTYPE_ARRAY );
	RTTI_ENUM_OPTION( TEXTYPE_3D );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( BufferGroup, GpuWrapApi );
{
	RTTI_ENUM_OPTION( System );
	RTTI_ENUM_OPTION( MeshResource );
	RTTI_ENUM_OPTION( MeshCustom );
	RTTI_ENUM_OPTION( AutoSpawner );
	RTTI_ENUM_OPTION( Debug );
	RTTI_ENUM_OPTION( DPL );
	RTTI_ENUM_OPTION( Weather );
	RTTI_ENUM_OPTION( ReflectionProbe );
	RTTI_ENUM_OPTION( Skinning );
	RTTI_ENUM_OPTION( Lights );
	RTTI_ENUM_OPTION( Video );
	RTTI_ENUM_OPTION( Particles );
	RTTI_ENUM_OPTION( GIManagerLitProbes ); 
	RTTI_ENUM_OPTION( GIManagerLookup );
	RTTI_ENUM_OPTION( GIManagerInterpolation );
	RTTI_ENUM_OPTION( GIManagerLitBricks );
	RTTI_ENUM_OPTION( GIManagerLights );
	RTTI_ENUM_OPTION( GIManagerEnvVolume );
	RTTI_ENUM_OPTION( GIProxyBrick );
	RTTI_ENUM_OPTION( GIProxySurfel );
	RTTI_ENUM_OPTION( GIProxyProbes );
	RTTI_ENUM_OPTION( GIProxyFactors );
	RTTI_ENUM_OPTION( GIProxyAcceleration );
	RTTI_ENUM_OPTION( Raytracing );
	RTTI_ENUM_OPTION( RaytracingUpload );
	RTTI_ENUM_OPTION( RaytracingAS );
	RTTI_ENUM_OPTION( Decals );
	RTTI_ENUM_OPTION( Instances );
	RTTI_ENUM_OPTION( Materials );
	RTTI_ENUM_OPTION( Multilayer );
	RTTI_ENUM_OPTION( FrameResources );
	RTTI_ENUM_OPTION( Misc );
	RTTI_ENUM_OPTION( MorphTargets );
	RTTI_ENUM_OPTION( MAX );
}
RTTI_END_ENUM();
static_assert( (Int32)GpuWrapApi::BufferGroup::MAX == 33, "Number of BufferGroup changed. Add it here if needed" );

RTTI_BEGIN_ENUM_IN_NAMESPACE( eBufferChunkCategory, GpuWrapApi );
{
	RTTI_ENUM_OPTION( BCC_Staging );
	RTTI_ENUM_OPTION( BCC_Vertex );
	RTTI_ENUM_OPTION( BCC_VertexUAV );
	RTTI_ENUM_OPTION( BCC_Index16Bit );
	RTTI_ENUM_OPTION( BCC_Index32Bit );
	RTTI_ENUM_OPTION( BCC_VertexIndex16Bit );
	RTTI_ENUM_OPTION( BCC_Constant );
	RTTI_ENUM_OPTION( BCC_TypedUAV );
	RTTI_ENUM_OPTION( BCC_Structured );
	RTTI_ENUM_OPTION( BCC_StructuredUAV );
	RTTI_ENUM_OPTION( BCC_StructuredAppendUAV );
	RTTI_ENUM_OPTION( BCC_IndirectUAV );
	RTTI_ENUM_OPTION( BCC_Index16BitUAV );
	RTTI_ENUM_OPTION( BCC_Raw );
	RTTI_ENUM_OPTION( BCC_Invalid );
}
RTTI_END_ENUM();
static_assert( (Int32)GpuWrapApi::BCC_Invalid == 14, "Number of eBufferChunkCategory changed. Add it here if needed" );

namespace GpuWrapApi
{
	///////////////////////

	const AnsiChar* GetTextureFormatName( GpuWrapApi::eTextureFormat format )
	{
		static_assert(GpuWrapApi::TEXFMT_Max == RED_CHECK_TEXFMT_COUNT, "Add new entries");

#define RENDERDATA_TEXFMT_NAME_CASE(fmt)	case fmt: return #fmt;
		switch( format )
		{
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_L8_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R8_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R8G8_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_B5G6R5_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R8G8B8A8_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R8G8B8A8_Unorm_SRGB );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R8G8B8A8_Uint_Typeless );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R8G8B8A8_Snorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R10G10B10A2_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R8G8B8X8_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16_Uint );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R32_Uint );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16G16B16A16_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16G16_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16G16_Snorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16G16B16A16_Uint );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16G16_Uint );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16G16_Sint );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R32G32_Uint );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R32G32B32A32_Uint );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16G16B16A16_Float );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R11G11B10_Float );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16G16_Float );
#ifdef RED_PLATFORM_DURANGO
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R10G10B10_6E4_A2_Float );
#endif
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R32G32_Float );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R32G32B32A32_Float );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R32_Float );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R16_Float );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_D24S8 );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_D32FS8 );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_NULL );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC1 );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC1_SRGB );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC2 );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC2_SRGB );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC3 );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC3_SRGB );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC4 );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC5 );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC6H_UNSIGNED );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC6H_SIGNED );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC7 );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_BC7_SRGB );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_A8_Unorm );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_D32F );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_D16U );
			RENDERDATA_TEXFMT_NAME_CASE( TEXFMT_R8_Uint );
		default:
			RED_HALT( "Unhandled texture format" );
			return "UNKNOWN_TEXFMT";
		}
#undef RENDERDATA_TEXFMT_NAME_CASE
	}

	///////////////////////

	Bool IsTextureFormatDepth( eTextureFormat format )
	{
		return
			TEXFMT_D24S8 == format ||
			TEXFMT_D32FS8 == format ||
			TEXFMT_D32F == format ||
			TEXFMT_D16U == format;
	}

	Bool IsTextureFormatSRGB( eTextureFormat format )
	{
		return
			format == TEXFMT_R8G8B8A8_Unorm_SRGB ||
			format == TEXFMT_BC1_SRGB ||
			format == TEXFMT_BC2_SRGB ||
			format == TEXFMT_BC3_SRGB ||
			format == TEXFMT_BC7_SRGB;
	}


	///////////////////////

	/// Calculate texture pitch for a given width and format.
	Uint32 CalculateUnpaddedTexturePitch( Uint32 width, GpuWrapApi::eTextureFormat format )
	{
		// Keep width > 0, so if caller is using something like mip0Width >> mipLevel with a non-square texture, we aren't going to end up
		// with 0 pitch.
		width = math::Max( width, 1u );


		static_assert(GpuWrapApi::TEXFMT_Max == RED_CHECK_TEXFMT_COUNT, "Add new entries");

		switch( format )
		{
		case GpuWrapApi::TEXFMT_L8_Unorm:
		case GpuWrapApi::TEXFMT_A8_Unorm:
		case GpuWrapApi::TEXFMT_R8_Unorm:
		case GpuWrapApi::TEXFMT_R8_Uint:
			return width;

		case GpuWrapApi::TEXFMT_R8G8_Unorm:
		case GpuWrapApi::TEXFMT_R16_Float:
		case GpuWrapApi::TEXFMT_R16_Unorm:
		case GpuWrapApi::TEXFMT_R16_Uint:
		case GpuWrapApi::TEXFMT_B5G6R5_Unorm:
			return width * 2;

		case GpuWrapApi::TEXFMT_R8G8B8X8_Unorm:
		case GpuWrapApi::TEXFMT_R8G8B8A8_Unorm:
		case GpuWrapApi::TEXFMT_R8G8B8A8_Unorm_SRGB:
		case GpuWrapApi::TEXFMT_R8G8B8A8_Snorm:
		case GpuWrapApi::TEXFMT_R8G8B8A8_Uint_Typeless:
		case GpuWrapApi::TEXFMT_R10G10B10A2_Unorm:
		case GpuWrapApi::TEXFMT_R11G11B10_Float:
		case GpuWrapApi::TEXFMT_R16G16_Float:
		case GpuWrapApi::TEXFMT_R16G16_Uint:
		case GpuWrapApi::TEXFMT_R16G16_Sint:
		case GpuWrapApi::TEXFMT_R32_Float:
		case GpuWrapApi::TEXFMT_R32_Uint:
		case GpuWrapApi::TEXFMT_R16G16_Unorm:
		case GpuWrapApi::TEXFMT_R16G16_Snorm:
#ifdef RED_PLATFORM_DURANGO
		case GpuWrapApi::TEXFMT_R10G10B10_6E4_A2_Float:
#endif
			return width * 4;

		case GpuWrapApi::TEXFMT_R16G16B16A16_Float:
		case GpuWrapApi::TEXFMT_R16G16B16A16_Unorm:
		case GpuWrapApi::TEXFMT_R16G16B16A16_Uint:
		case GpuWrapApi::TEXFMT_R32G32_Float:
		case GpuWrapApi::TEXFMT_R32G32_Uint:
			return width * 8;

		case GpuWrapApi::TEXFMT_R32G32B32A32_Float:
		case GpuWrapApi::TEXFMT_R32G32B32A32_Uint:
			return width * 16;

		case GpuWrapApi::TEXFMT_BC1:
		case GpuWrapApi::TEXFMT_BC1_SRGB:
		case GpuWrapApi::TEXFMT_BC4:
			return red::AlignUp( width, 4u ) * 2u;

		case GpuWrapApi::TEXFMT_BC2:
		case GpuWrapApi::TEXFMT_BC2_SRGB:
		case GpuWrapApi::TEXFMT_BC3:
		case GpuWrapApi::TEXFMT_BC3_SRGB:
		case GpuWrapApi::TEXFMT_BC5:
		case GpuWrapApi::TEXFMT_BC6H_UNSIGNED:
		case GpuWrapApi::TEXFMT_BC6H_SIGNED:
		case GpuWrapApi::TEXFMT_BC7:
		case GpuWrapApi::TEXFMT_BC7_SRGB:
			return red::AlignUp( width, 4u ) * 4u;

		case GpuWrapApi::TEXFMT_D24S8:
		case GpuWrapApi::TEXFMT_D32F:
			return width * 4;

		default:
			RED_HALT( "Not supported yet. Please add a case for texture format %s", GetTextureFormatName( format ) );
			return 0;
		}
	}

	///////////////////////

	/// Calculate byte-size of data for a texture with given size and format. Just one subresource, not a full mip chain.
	Uint32 CalculateUnpaddedTextureSize( Uint32 width, Uint32 height, GpuWrapApi::eTextureFormat format )
	{
		Uint32 pitch = CalculateUnpaddedTexturePitch( width, format );

		// Keep height > 0, so if caller is using something like mip0Height >> mipLevel with a non-square texture, we aren't going to end up
		// with 0 size. We don't need to do the same with width, since CalculateTexturePitch takes care of that for us.
		height = math::Max( height, 1u );

		switch( format )
		{
			// Compressed formats combine multiple rows in the image into one data row.
		case GpuWrapApi::TEXFMT_BC1:
		case GpuWrapApi::TEXFMT_BC1_SRGB:
		case GpuWrapApi::TEXFMT_BC2:
		case GpuWrapApi::TEXFMT_BC2_SRGB:
		case GpuWrapApi::TEXFMT_BC3:
		case GpuWrapApi::TEXFMT_BC3_SRGB:
		case GpuWrapApi::TEXFMT_BC4:
		case GpuWrapApi::TEXFMT_BC5:
		case GpuWrapApi::TEXFMT_BC6H_UNSIGNED:
		case GpuWrapApi::TEXFMT_BC6H_SIGNED:
		case GpuWrapApi::TEXFMT_BC7:
		case GpuWrapApi::TEXFMT_BC7_SRGB:
			return pitch * math::Max( ( height + 3 ) / 4, 1u );

		default:
			return pitch * height;
		}
	}

	///////////////////////

	const AnsiChar* GetTextureGroupName( const eTextureGroup group )
	{
		static_assert( TEXG_System_End - TEXG_System_Begin == 19, "Add missing TEX_SYSTEM groups" );
		static_assert( TEXG_Generic_End - TEXG_Generic_Begin == 7, "Add missing TEX_GENERIC groups" );
		static_assert( TEXG_Multilayer_End - TEXG_Multilayer_Begin == 4, "Add missing TEX_MULTILAYER groups" );
		static_assert( TEXG_MAX == 34, "Add missing groups");
		switch( group )
		{
		case TEXG_Unknown:					return "Unknown";

			// Resources & assets
		case TEXG_Generic_Color:			return "GenericColor";
		case TEXG_Generic_Grayscale:		return "Grayscale";
		case TEXG_Generic_Normal:			return "GenericNormal";
		case TEXG_Generic_Data:				return "Data";
		case TEXG_Generic_UI:				return "UI";
		case TEXG_Generic_Font:				return "Font";
		case TEXG_Generic_LUT:				return "LUT";
		case TEXG_Generic_MorphBlend:		return "MorphBlend";

		case TEXG_Multilayer_Color:			return "MultilayerColor";
		case TEXG_Multilayer_Normal:		return "MultilayerNormal";
		case TEXG_Multilayer_Grayscale:		return "MultilayerGrayscale";
		case TEXG_Multilayer_Microblend:	return "MultilayerMicroblend";
		case TEXG_Multilayer_MLMask:		return "MultilayerMLMask";

			// System
		case TEXG_System_Generic:			return "SystemGeneric";
		case TEXG_System_RenderTarget:		return "SystemRenderTarget";
		case TEXG_System_RenderTargetFlowAllocator:		return "SystemRenderTargetFlowAllocator";
		case TEXG_System_RenderTargetUI_Atlas:		return "SystemRenderTargetUI_Atlas";
		case TEXG_System_RenderTargetUI_Pool:		return "SystemRenderTargetUI_Pool";
		case TEXG_System_RenderTargetUI_Manager:	return "SystemRenderTargetUI_Manager";
		case TEXG_System_RenderTargetUI_HUDLayer:	return "SystemRenderTargetUI_HUDLayer";
		case TEXG_System_RenderTargetUI_CacheWidget:return "SystemRenderTargetUI_CacheWidget";
		case TEXG_System_RayTracing:		return "SystemRayTracing";
		case TEXG_System_GameplayFX:		return "SystemGameplayFX";
		case TEXG_System_PostProcess:		return "SystemPostProcess";
		case TEXG_System_Environment:		return "SystemEnvironment";
		case TEXG_System_EnvProbe:			return "SystemEnvProbe";
		case TEXG_System_Shadows:			return "SystemShadows";
		case TEXG_System_Terrain:			return "SystemTerrain";
		case TEXG_System_GI:				return "SystemGI";
		case TEXG_System_MultilayerProxy:	return "SystemMultilayerProxy";
		case TEXG_System_Video:				return "SystemVideo";
		case TEXG_System_Garment:			return "SystemGarment";
		case TEXG_System_MorphTargets:		return "SystemMorphTargets";

		default:
			RED_HALT( "Unhandled texture group" );
		}
		return "Unknown";
	}

	eTextureGroup GetTextureGroupByName( const AnsiChar* groupName )
	{
		static_assert( TEXG_System_End - TEXG_System_Begin == 19, "Add missing TEX_SYSTEM groups" );
		static_assert( TEXG_Generic_End - TEXG_Generic_Begin == 7, "Add missing TEX_GENERIC groups" );
		static_assert( TEXG_Multilayer_End - TEXG_Multilayer_Begin == 4, "Add missing TEX_MULTILAYER groups" );
		static_assert( TEXG_MAX == 34, "Add missing groups" );

		switch( red::CalculateHash32( groupName ) )
		{
			// Resources & assets
		case red::CalculateHash32( "GenericColor" ):			return TEXG_Generic_Color;
		case red::CalculateHash32( "Grayscale" ):				return TEXG_Generic_Grayscale;
		case red::CalculateHash32( "GenericNormal" ):			return TEXG_Generic_Normal;
		case red::CalculateHash32( "Data" ):					return TEXG_Generic_Data;
		case red::CalculateHash32( "GenericUI" ):				return TEXG_Generic_UI;
		case red::CalculateHash32( "Font" ):					return TEXG_Generic_Font;
		case red::CalculateHash32( "LUT" ):						return TEXG_Generic_LUT;
		case red::CalculateHash32( "MorphBlend" ):				return TEXG_Generic_MorphBlend;

		case red::CalculateHash32( "MultilayerColor" ):			return TEXG_Multilayer_Color;
		case red::CalculateHash32( "MultilayerNormal" ):		return TEXG_Multilayer_Normal;
		case red::CalculateHash32( "MultilayerGrayscale" ):		return TEXG_Multilayer_Grayscale;
		case red::CalculateHash32( "MultilayerMicroblend" ):	return TEXG_Multilayer_Microblend;
		case red::CalculateHash32( "MultilayerMLMask" ):		return TEXG_Multilayer_MLMask;

			// System
		case red::CalculateHash32( "SystemGeneric" ):			return TEXG_System_Generic;
		case red::CalculateHash32( "SystemRenderTarget" ):		return TEXG_System_RenderTarget;
		case red::CalculateHash32( "SystemRenderTargetFlowAllocator" ):		return TEXG_System_RenderTargetFlowAllocator;
		case red::CalculateHash32( "SystemRenderTargetUI_Atlas" ):		return TEXG_System_RenderTargetUI_Atlas;
		case red::CalculateHash32( "SystemRenderTargetUI_Pool" ):		return TEXG_System_RenderTargetUI_Pool;
		case red::CalculateHash32( "SystemRenderTargetUI_Manager" ):	return TEXG_System_RenderTargetUI_Manager;
		case red::CalculateHash32( "SystemRenderTargetUI_HUDLayer" ):	return TEXG_System_RenderTargetUI_HUDLayer;
		case red::CalculateHash32( "SystemRenderTargetUI_CacheWidget" ):return TEXG_System_RenderTargetUI_CacheWidget;
		case red::CalculateHash32( "SystemRayTracing" ):		return TEXG_System_RayTracing;
		case red::CalculateHash32( "SystemGameplayFX" ):		return TEXG_System_GameplayFX;
		case red::CalculateHash32( "SystemPostProcess" ):		return TEXG_System_PostProcess;
		case red::CalculateHash32( "SystemEnvironment" ):		return TEXG_System_Environment;
		case red::CalculateHash32( "SystemEnvProbe" ):			return TEXG_System_EnvProbe;
		case red::CalculateHash32( "SystemShadows" ):			return TEXG_System_Shadows;
		case red::CalculateHash32( "SystemTerrain" ):			return TEXG_System_Terrain;
		case red::CalculateHash32( "SystemGI" ):				return TEXG_System_GI;
		case red::CalculateHash32( "SystemMultilayerProxy" ):	return TEXG_System_MultilayerProxy;
		case red::CalculateHash32( "SystemVideo" ):				return TEXG_System_Video;
		case red::CalculateHash32( "SystemGarment" ):			return TEXG_System_Garment;
		case red::CalculateHash32( "SystemMorphTargets" ):		return TEXG_System_MorphTargets;
		}

		return TEXG_Unknown;
	}


	const AnsiChar* GetBufferGroupName( const BufferGroup group )
	{
		static_assert(Uint32(BufferGroup::MAX) == 33, "BufferGroup has changed");
		switch ( group )
		{
		case BufferGroup::System:						return "System";
		case BufferGroup::MeshResource:					return "MeshResource";
		case BufferGroup::MeshCustom:					return "MeshCustom";
		case BufferGroup::AutoSpawner:					return "AutoSpawner";
		case BufferGroup::Debug:						return "Debug";
		case BufferGroup::DPL:							return "DPL";
		case BufferGroup::Weather:						return "Weather";
		case BufferGroup::ReflectionProbe:				return "ReflectionProbe";
		case BufferGroup::Skinning:						return "Skinning";
		case BufferGroup::Lights:						return "Lights";
		case BufferGroup::Video:						return "Video";
		case BufferGroup::Particles:					return "Particles";
		case BufferGroup::GIManagerLitProbes:			return "GIManagerLitProbes";
		case BufferGroup::GIManagerLookup:				return "GIManagerLookup";
		case BufferGroup::GIManagerInterpolation:		return "GIManagerInterpolation";
		case BufferGroup::GIManagerLitBricks:			return "GIManagerLitBricks";
		case BufferGroup::GIManagerLights:				return "GIManagerLights";
		case BufferGroup::GIManagerEnvVolume:			return "GIManagerEnvVolume";
		case BufferGroup::GIProxyBrick:					return "GIProxyBrick";
		case BufferGroup::GIProxySurfel:				return "GIProxySurfel";
		case BufferGroup::GIProxyProbes:				return "GIProxyProbes";
		case BufferGroup::GIProxyFactors:				return "GIProxyFactors";
		case BufferGroup::GIProxyAcceleration:			return "GIProxyAcceleration";
		case BufferGroup::Raytracing:					return "Raytracing";
		case BufferGroup::RaytracingUpload:				return "RaytracingUpload";
		case BufferGroup::RaytracingAS:					return "RaytracingAS";
		case BufferGroup::Decals:						return "Decals";
		case BufferGroup::Instances:					return "Instances";
		case BufferGroup::Materials:					return "Materials";
		case BufferGroup::Multilayer:					return "Multilayer";
		case BufferGroup::FrameResources:				return "FrameResources";
		case BufferGroup::Misc:							return "Misc";
		case BufferGroup::MorphTargets:					return "MorphTargets";
		default: return "UNKNOWN";
		}
	}

	eTextureFormat GetHdrModeTextureFormat( eHDRMode hdrMode )
	{
		switch( hdrMode )
		{
		case HDRMODE_None:
			return TEXFMT_R8G8B8A8_Unorm;
		case HDRMODE_HDR10_PQ:
		case HDRMODE_FreeSync2_Gamma22:
			return TEXFMT_R10G10B10A2_Unorm;
		case HDRMODE_HDR10_scRGB:
		case HDRMODE_FreeSync2_scRGB:
			return TEXFMT_R16G16B16A16_Float;

		default:
			RED_ASSERT( 0 && "GetHdrModeTextureFormat: Invalid HDR mode" );
			return TEXFMT_R8G8B8A8_Unorm;
		}
	}

	///////////////////////

	// Name builders
	const AnsiChar* GetShaderTypeName( eShaderType shaderType )
	{
		static_assert(ShaderTypeMax == 4u, "Update shaderType names");

#define RENDERDATA_SHADER_TYPE_NAME_CASE(type)	case type: return #type;
		switch( shaderType )
		{
			RENDERDATA_SHADER_TYPE_NAME_CASE( VertexShader );
			RENDERDATA_SHADER_TYPE_NAME_CASE( PixelShader );
			RENDERDATA_SHADER_TYPE_NAME_CASE( ComputeShader );
			RENDERDATA_SHADER_TYPE_NAME_CASE( ShaderLibrary );
		default:
			return "UNKNOWN_SHADER_TYPE";
		}
#undef RENDERDATA_SHADER_TYPE_NAME_CASE
	}

	///////////////////////

	eShaderType ShaderTypeFromName( const AnsiChar* shaderTypeName )
	{
		static_assert(ShaderTypeMax == 4u, "Update shaderType names");

#define RENDERDATA_SHADER_TYPE_FROM_NAME(type)	if ( red::Strcmp( shaderTypeName, #type ) == 0 ) return type;

		RENDERDATA_SHADER_TYPE_FROM_NAME( VertexShader );
		RENDERDATA_SHADER_TYPE_FROM_NAME( PixelShader );
		RENDERDATA_SHADER_TYPE_FROM_NAME( ComputeShader );
		RENDERDATA_SHADER_TYPE_FROM_NAME( ShaderLibrary );

		return ShaderTypeMax;
#undef RENDERDATA_SHADER_TYPE_FROM_NAME
	}

	///////////////////////

} // end namespace GpuWrapApi