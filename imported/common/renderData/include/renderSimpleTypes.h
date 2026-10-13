/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "renderDataApi.h"
#include "../../redSystem/include/bitUtils.h"


namespace GpuWrapApi // acseparate temporary namespace name
{

	// - - - - - - - - - -

	template <typename Enum, typename BaseType = Uint64>
	class SimpleEnumMask
	{
		BaseType			m_mask;

	public:

		class Iterator
		{
			BaseType	m_mask;
			Enum		m_current;
		public:
			RED_FORCE_INLINE Iterator( const SimpleEnumMask& mask ) : m_mask( mask.Get() ) { if( m_mask ) m_current = Enum( red::BitUtils::BitScanForward( m_mask ) ); }
			RED_FORCE_INLINE operator Bool() const { return m_mask != 0; }
			RED_FORCE_INLINE operator Enum() const { return m_current; }
			RED_FORCE_INLINE void operator ++() { m_mask &= ~RED_FLAG64(m_current); if( m_mask ) m_current = Enum( red::BitUtils::BitScanForward( m_mask ) ); }
		};

	protected:

		RED_FORCE_INLINE SimpleEnumMask( BaseType mask ) : m_mask( mask ) {}

	public:

		RED_FORCE_INLINE SimpleEnumMask() : m_mask( 0 ) {}
		RED_FORCE_INLINE SimpleEnumMask( Enum a ) : m_mask( RED_FLAG64(a) ) {}
		RED_FORCE_INLINE SimpleEnumMask( Enum a, Enum b ) : m_mask( RED_FLAG64(a) | RED_FLAG64(b) ) {}
		RED_FORCE_INLINE SimpleEnumMask( Enum a, Enum b, Enum c ) : m_mask( RED_FLAG64(a) | RED_FLAG64(b) | RED_FLAG64(c) ) {}
		RED_FORCE_INLINE SimpleEnumMask( Enum a, Enum b, Enum c, Enum d ) : m_mask( RED_FLAG64(a) | RED_FLAG64(b) | RED_FLAG64(c) | RED_FLAG64(d) ) {}
		RED_FORCE_INLINE SimpleEnumMask( Enum a, Enum b, Enum c, Enum d, Enum e ) : m_mask( RED_FLAG64(a) | RED_FLAG64(b) | RED_FLAG64(c) | RED_FLAG64(d) | RED_FLAG64(e) ) {}
		RED_FORCE_INLINE SimpleEnumMask( Enum a, Enum b, Enum c, Enum d, Enum e, Enum f ) : m_mask( RED_FLAG64(a) | RED_FLAG64(b) | RED_FLAG64(c) | RED_FLAG64(d) | RED_FLAG64(e) | RED_FLAG64(f) ) {}

		RED_FORCE_INLINE operator BaseType() const { return m_mask; }
		RED_FORCE_INLINE operator Bool() const { return m_mask != 0; }
		RED_FORCE_INLINE BaseType Get() const { return m_mask; }
		RED_FORCE_INLINE Iterator GetIterator() const { return Iterator( *this ); }

		RED_FORCE_INLINE Bool Has( Enum other ) const { return (m_mask & RED_FLAG64(other)) == RED_FLAG64(other); }

		RED_FORCE_INLINE  SimpleEnumMask& operator&=( const SimpleEnumMask& other ) { m_mask &= other.m_mask; return *this; }
		RED_FORCE_INLINE  SimpleEnumMask& operator^=( const SimpleEnumMask& other ) { m_mask ^= other.m_mask; return *this; }
		RED_FORCE_INLINE  SimpleEnumMask& operator|=( const SimpleEnumMask& other ) { m_mask |= other.m_mask; return *this; }

		RED_FORCE_INLINE  SimpleEnumMask operator&( const SimpleEnumMask& other ) const { return SimpleEnumMask( m_mask & other.m_mask ); }
		RED_FORCE_INLINE  SimpleEnumMask operator^( const SimpleEnumMask& other ) const { return SimpleEnumMask( m_mask ^ other.m_mask ); }
		RED_FORCE_INLINE  SimpleEnumMask operator|( const SimpleEnumMask& other ) const { return SimpleEnumMask( m_mask | other.m_mask ); }

		RED_FORCE_INLINE  SimpleEnumMask& operator&=( Enum other ) { m_mask &= RED_FLAG64(other); return *this; }
		RED_FORCE_INLINE  SimpleEnumMask& operator^=( Enum other ) { m_mask ^= RED_FLAG64(other); return *this; }
		RED_FORCE_INLINE  SimpleEnumMask& operator|=( Enum other ) { m_mask |= RED_FLAG64(other); return *this; }

		RED_FORCE_INLINE  SimpleEnumMask operator&( Enum other ) const { return SimpleEnumMask( m_mask & RED_FLAG64(other) ); }
		RED_FORCE_INLINE  SimpleEnumMask operator^( Enum other ) const { return SimpleEnumMask( m_mask ^ RED_FLAG64(other) ); }
		RED_FORCE_INLINE  SimpleEnumMask operator|( Enum other ) const { return SimpleEnumMask( m_mask | RED_FLAG64(other) ); }

		RED_FORCE_INLINE Bool operator ==( Enum other ) const { return m_mask == RED_FLAG64(other); }
		RED_FORCE_INLINE Bool operator !=( Enum other ) const { return m_mask != RED_FLAG64(other); }
	};

	// - - - - - - - - - -

#if defined( RED_PLATFORM_WINPC )

#define  SHADER_BASE_PATH			"shaders/"

#elif defined( RED_PLATFORM_DURANGO )

#define  SHADER_BASE_PATH			"shaders/"

#elif defined( RED_PLATFORM_ORBIS )

#define  SHADER_BASE_PATH			"shaders/"

#elif defined( RED_PLATFORM_LINUX )

#define  SHADER_BASE_PATH			"shaders/"

#else

#error Unsupported platform

#endif

	RED_INLINE const char*	GetShaderRootPath()
	{
		return SHADER_BASE_PATH;
	}

	RED_INLINE const AnsiChar*	GetShaderRootPathAnsi()
	{
		return SHADER_BASE_PATH;
	}

	RED_INLINE const char* GetShaderIncludePath()
	{
		return SHADER_BASE_PATH "include/";
	}

	RED_INLINE const AnsiChar* GetShaderIncludePathAnsi()
	{
		return SHADER_BASE_PATH "include/";
	}

	// - - - - - - - - - -

	enum eTextureSaveFormat : Uint8
	{
		SAVE_FORMAT_DDS,
		SAVE_FORMAT_BMP,
		SAVE_FORMAT_PNG,
		SAVE_FORMAT_JPG,
		SAVE_FORMAT_TGA,
		SAVE_FORMAT_RAW,
	};

	// - - - - - - - - - -
	
	enum eTextureImportFormat : Uint8
	{
		TIF_DDS,
		TIF_TGA,
		TIF_WIC,
		TIF_EXR,
		TIF_CUBE,
	};

	// - - - - - - - - - -

	enum eTextureType : Uint8
	{
		TEXTYPE_2D,		
		TEXTYPE_CUBE,
		TEXTYPE_ARRAY,
		TEXTYPE_3D,
		TEXTYPE_MAX
	};

	enum class eTextureSampleCount : Uint8
	{
		SampleCount1,
		SampleCount2,
		SampleCount4,
		SampleCount8,
	};

	// - - - - - - - - - -

	enum eShaderType : Uint8
	{
		VertexShader	,
		PixelShader		,
		ComputeShader	,
		ShaderLibrary	,
		ShaderTypeMax	,
		ShaderType_NonRayTraceMax = ShaderLibrary,
		ShaderType_GraphicsMax = ComputeShader,
	};

	typedef  SimpleEnumMask<eShaderType,Uint32> ShaderTypeMask;

	RENDER_DATA_API const AnsiChar* GetShaderTypeName( eShaderType shaderType );
	RENDER_DATA_API eShaderType ShaderTypeFromName( const AnsiChar* shaderTypeName );

	// - - - - - - - - - -

	enum eBufferChunkCategory : Uint8
	{
		BCC_Staging,					// No resource views, just usable as source for copying.
		BCC_Vertex,
		BCC_VertexUAV,
		BCC_Index16Bit,
		BCC_Index32Bit,
		BCC_VertexIndex16Bit,
		BCC_Constant,
		BCC_TypedUAV,
		BCC_Structured,
		BCC_StructuredUAV,
		BCC_StructuredAppendUAV,
		BCC_IndirectUAV,				// Can be viewed as Byte Buffer in shader
		BCC_Index16BitUAV,
		BCC_Raw,

		BCC_Invalid
	};
		
	// - - - - - - - - - -

	// NOTE : Any changes to texture formats may break texture cooking. The automated cooks should be fine, but if you need to
	// do a cook yourself, you may need to rebuild the TexCookTool projects in /dev/internal/TexCookTool.
	enum eTextureFormat : Uint8
	{
		// 8 Bit
		TEXFMT_A8_Unorm,
		TEXFMT_R8_Unorm,
		TEXFMT_L8_Unorm,
		TEXFMT_R8G8_Unorm,
		TEXFMT_R8G8B8X8_Unorm,
		TEXFMT_R8G8B8A8_Unorm,		
		TEXFMT_R8G8B8A8_Snorm,

		// Uints
		TEXFMT_R16_Unorm,
		TEXFMT_R16_Uint,
		TEXFMT_R32_Uint,
		TEXFMT_R32G32B32A32_Uint,
		TEXFMT_R32G32_Uint,
		TEXFMT_R16G16B16A16_Unorm,
		TEXFMT_R16G16B16A16_Uint,
		TEXFMT_R16G16_Uint,
		TEXFMT_R10G10B10A2_Unorm,

		// Float
		TEXFMT_R16G16B16A16_Float,		
		TEXFMT_R11G11B10_Float,	
		TEXFMT_R16G16_Float,
		TEXFMT_R32G32_Float, // dex
		TEXFMT_R32G32B32A32_Float,
		TEXFMT_R32_Float,
		TEXFMT_R16_Float,

		// Depth and stencil
		TEXFMT_D24S8,
		TEXFMT_D32FS8,
		TEXFMT___unused,// UNUSED
		TEXFMT_D32F,	// ORBIS only: 32bit float depth
		TEXFMT_D16U,	// ORBIS only: 16 bit unsigned int depth

		// Compressed
		TEXFMT_BC1,
		TEXFMT_BC2,
		TEXFMT_BC3,
		TEXFMT_BC4,
		TEXFMT_BC5,

		// DirectX 11 only
		TEXFMT_BC6H_UNSIGNED,
		TEXFMT_BC6H_SIGNED,
		TEXFMT_BC7,
		TEXFMT_BC7_SRGB,

		TEXFMT_R8_Uint,

		TEXFMT_R8G8B8A8_Uint_Typeless,	// for morphs

		// TODO these should be moved up to proper groups, but we already serialized the values into files...
		TEXFMT_R8G8B8A8_Unorm_SRGB,
		TEXFMT_BC1_SRGB,
		TEXFMT_BC2_SRGB,
		TEXFMT_BC3_SRGB,

		TEXFMT_R16G16_Unorm,
		TEXFMT_R16G16_Sint,
		TEXFMT_R16G16_Snorm,

		TEXFMT_B5G6R5_Unorm,

		// ifdef'd stuff moved to end, so formats stored in cooked data is correct regardless of platform.
#ifdef RED_PLATFORM_DURANGO
		TEXFMT_R10G10B10_6E4_A2_Float,
#endif

		// Special
		TEXFMT_NULL,

		TEXFMT_Max
	};

#ifdef RED_PLATFORM_DURANGO
#	define RED_CHECK_TEXFMT_COUNT 49
#else
#	define RED_CHECK_TEXFMT_COUNT 48
#endif

	// - - - - - - - - - -

	inline Bool IsTextureFormatDXT( GpuWrapApi::eTextureFormat format )
	{
		return TEXFMT_BC1 == format 
			|| TEXFMT_BC1_SRGB == format 
			|| TEXFMT_BC2 == format 
			|| TEXFMT_BC2_SRGB == format
			|| TEXFMT_BC3 == format 
			|| TEXFMT_BC3_SRGB == format
			|| TEXFMT_BC4 == format 
			|| TEXFMT_BC5 == format 
			|| TEXFMT_BC6H_UNSIGNED == format 
			|| TEXFMT_BC6H_SIGNED == format
			|| TEXFMT_BC7 == format
			|| TEXFMT_BC7_SRGB == format;
	}

	RED_INLINE Bool IsSupportedCompressedFormat( GpuWrapApi::eTextureFormat format )
	{
		switch ( format )
		{
		case GpuWrapApi::TEXFMT_BC1:
		case GpuWrapApi::TEXFMT_BC1_SRGB:
		case GpuWrapApi::TEXFMT_BC3:
		case GpuWrapApi::TEXFMT_BC3_SRGB:
		case GpuWrapApi::TEXFMT_BC4:
		case GpuWrapApi::TEXFMT_BC5:
			return true;

		default:
			return false;
		}

		static_assert( GpuWrapApi::TEXFMT_Max == RED_CHECK_TEXFMT_COUNT, "Add new entries" );
	}

	// - - - - - - - - - -

	RENDER_DATA_API const AnsiChar* GetTextureFormatName( GpuWrapApi::eTextureFormat format );
	RENDER_DATA_API Bool IsTextureFormatDepth( eTextureFormat format );
	RENDER_DATA_API Bool IsTextureFormatSRGB( eTextureFormat format );

	/// Calculate texture pitch for a given width and format.
	RENDER_DATA_API Uint32 CalculateUnpaddedTexturePitch( Uint32 width, GpuWrapApi::eTextureFormat format );
	/// Calculate byte-size of data for a texture with given size and format. Just one subresource, not a full mip chain.
	RENDER_DATA_API Uint32 CalculateUnpaddedTextureSize( Uint32 width, Uint32 height, GpuWrapApi::eTextureFormat format );


	RED_INLINE Uint32 GetBitsPerPixel( GpuWrapApi::eTextureFormat format )
	{
		switch ( format )
		{
		case GpuWrapApi::TEXFMT_A8_Unorm:				return 8;
		case GpuWrapApi::TEXFMT_R8_Unorm:				return 8;
		case GpuWrapApi::TEXFMT_L8_Unorm:				return 8;
		case GpuWrapApi::TEXFMT_R8_Uint:				return 8;
		case GpuWrapApi::TEXFMT_R8G8_Unorm:				return 16;
		case GpuWrapApi::TEXFMT_B5G6R5_Unorm:			return 16;
		case GpuWrapApi::TEXFMT_R8G8B8A8_Unorm:			return 32;
		case GpuWrapApi::TEXFMT_R8G8B8A8_Unorm_SRGB:	return 32;
		case GpuWrapApi::TEXFMT_R8G8B8A8_Uint_Typeless:	return 32;
		case GpuWrapApi::TEXFMT_R8G8B8A8_Snorm:			return 32;
		case GpuWrapApi::TEXFMT_R10G10B10A2_Unorm:		return 32;
		case GpuWrapApi::TEXFMT_R8G8B8X8_Unorm:			return 32;
		case GpuWrapApi::TEXFMT_R16_Unorm:				return 16;
		case GpuWrapApi::TEXFMT_R16_Uint:				return 16;
		case GpuWrapApi::TEXFMT_R32_Uint:				return 32;
		case GpuWrapApi::TEXFMT_R16G16_Uint:			return 32;
		case GpuWrapApi::TEXFMT_R16G16_Sint:			return 32;
		case GpuWrapApi::TEXFMT_R32G32_Uint:			return 64;
		case GpuWrapApi::TEXFMT_R32G32B32A32_Uint:		return 128;
		case GpuWrapApi::TEXFMT_R16G16B16A16_Float:		return 64;
		case GpuWrapApi::TEXFMT_R16G16B16A16_Unorm:		return 64;
		case GpuWrapApi::TEXFMT_R16G16_Unorm:			return 32;
		case GpuWrapApi::TEXFMT_R16G16_Snorm:			return 32;
		case GpuWrapApi::TEXFMT_R16G16B16A16_Uint:		return 64;
		case GpuWrapApi::TEXFMT_R11G11B10_Float:		return 32;	
		case GpuWrapApi::TEXFMT_R16G16_Float:			return 32;
#ifdef RED_PLATFORM_DURANGO
		case GpuWrapApi::TEXFMT_R10G10B10_6E4_A2_Float:	return 32;
#endif
		case GpuWrapApi::TEXFMT_R32G32_Float:			return 64;
		case GpuWrapApi::TEXFMT_R32G32B32A32_Float:		return 128;
		case GpuWrapApi::TEXFMT_R32_Float:				return 32;
		case GpuWrapApi::TEXFMT_R16_Float:				return 16;
		case GpuWrapApi::TEXFMT_D24S8:					return 32 + 8; // Note: 24-bit depth buffer is emulated using 32-bit floats
		case GpuWrapApi::TEXFMT_D16U:					return 16;
		case GpuWrapApi::TEXFMT_D32F:					return 32;
		case GpuWrapApi::TEXFMT_D32FS8:					return 32 + 8;
		case GpuWrapApi::TEXFMT_BC1:					return 4;
		case GpuWrapApi::TEXFMT_BC1_SRGB:				return 4;
		case GpuWrapApi::TEXFMT_BC2:					return 8;
		case GpuWrapApi::TEXFMT_BC2_SRGB:				return 8;
		case GpuWrapApi::TEXFMT_BC3:					return 8;
		case GpuWrapApi::TEXFMT_BC3_SRGB:				return 8;
		case GpuWrapApi::TEXFMT_BC4:					return 4;
		case GpuWrapApi::TEXFMT_BC5:					return 8;
		case GpuWrapApi::TEXFMT_BC6H_UNSIGNED:			return 8;
		case GpuWrapApi::TEXFMT_BC6H_SIGNED:			return 8;
		case GpuWrapApi::TEXFMT_BC7:					return 8;
		case GpuWrapApi::TEXFMT_BC7_SRGB:				return 8;
		case GpuWrapApi::TEXFMT_NULL:					return 32;
		default: RED_HALT("Unhandled texture format %u", format); return 0;
		}

		static_assert( GpuWrapApi::TEXFMT_Max == RED_CHECK_TEXFMT_COUNT, "Add new entries" );
	}


	// - - - - - - - - - -

	enum eTextureGroup : Uint8
	{
		TEXG_Unknown,
		// - - - - - - - - - - - - - - - - - - - - - - -
		TEXG_Generic_Begin,

			TEXG_Generic_Color = TEXG_Generic_Begin,
			TEXG_Generic_Grayscale,
			TEXG_Generic_Normal,
			TEXG_Generic_Data,
			TEXG_Generic_UI,
			TEXG_Generic_Font,
			TEXG_Generic_LUT,
			TEXG_Generic_MorphBlend,

		TEXG_Generic_End = TEXG_Generic_MorphBlend,
		// - - - - - - - - - - - - - - - - - - - - - - -
		TEXG_Multilayer_Begin,

			TEXG_Multilayer_Color = TEXG_Multilayer_Begin,
			TEXG_Multilayer_Normal,
			TEXG_Multilayer_Grayscale,
			TEXG_Multilayer_Microblend,
			TEXG_Multilayer_MLMask,

		TEXG_Multilayer_End = TEXG_Multilayer_MLMask,
		// - - - - - - - - - - - - - - - - - - - - - - -
		TEXG_System_Begin,

			TEXG_System_Generic = TEXG_System_Begin,
			TEXG_System_RenderTarget,
			TEXG_System_RenderTargetFlowAllocator,
			TEXG_System_RenderTargetUI_Begin, 

				TEXG_System_RenderTargetUI_Atlas = TEXG_System_RenderTargetUI_Begin,
				TEXG_System_RenderTargetUI_Pool,
				TEXG_System_RenderTargetUI_Manager,
				TEXG_System_RenderTargetUI_HUDLayer,
				TEXG_System_RenderTargetUI_CacheWidget,

			TEXG_System_RenderTargetUI_End = TEXG_System_RenderTargetUI_CacheWidget,
			TEXG_System_RayTracing,
			TEXG_System_GameplayFX,
			TEXG_System_PostProcess,
			TEXG_System_Environment,
			TEXG_System_EnvProbe,
			TEXG_System_Shadows,
			TEXG_System_Terrain,
			TEXG_System_GI,
			TEXG_System_MultilayerProxy,
			TEXG_System_Video,
			TEXG_System_Garment,
			TEXG_System_MorphTargets,

		TEXG_System_End = TEXG_System_MorphTargets,
		// - - - - - - - - - - - - - - - - - - - - - - -
		TEXG_MAX,
	};

	RED_FORCE_INLINE Bool IsTextureGroupSystem( const eTextureGroup group )
	{
		return group >= TEXG_System_Begin && group <= TEXG_System_End;
	}

	RED_FORCE_INLINE Bool IsTextureGroupGeneric( const eTextureGroup group )
	{
		return group >= TEXG_Generic_Begin && group <= TEXG_Generic_End;
	}

	RED_FORCE_INLINE Bool IsTextureGroupStreamable( const eTextureGroup group )
	{
		return IsTextureGroupGeneric( group ) && group != TEXG_Generic_UI;
	}

	RENDER_DATA_API const AnsiChar* GetTextureGroupName( const eTextureGroup group );

	RENDER_DATA_API eTextureGroup	GetTextureGroupByName( const AnsiChar* groupName );

	// - - - - - - - - - -

	enum EImageCompressionHint : Uint8
	{
		CIH_None,
		CIH_NormalmapRGB,
	};

	// - - - - - - - - - -


	enum class BufferGroup : Uint8
	{
		System,
		MeshResource,
		MeshCustom,			// custom index buffers, destruction, dismemberment
		AutoSpawner,
		Debug,
		DPL,
		Weather,			// fog, rain
		ReflectionProbe,
		Skinning,
		Lights,
		Video,
		Particles,
		GIManagerLitProbes, // GIManagerStart
		GIManagerLookup,
		GIManagerInterpolation,
		GIManagerLitBricks,
		GIManagerLights,
		GIManagerEnvVolume, // GIManagerEnd
		GIProxyBrick, // GIProxyStart
		GIProxySurfel,
		GIProxyProbes,
		GIProxyFactors,
		GIProxyAcceleration, // GIProxyEnd
		Raytracing,
		RaytracingUpload,
		RaytracingAS,
		Decals,
		Instances,
		Materials,
		Multilayer,
		FrameResources,
		Misc,
		MorphTargets,
		MAX
	};

	RENDER_DATA_API const AnsiChar* GetBufferGroupName( const BufferGroup group );

	enum eHDRMode : Uint8
	{
		HDRMODE_None,
		HDRMODE_HDR10_PQ,
		HDRMODE_HDR10_scRGB,
		HDRMODE_FreeSync2_Gamma22,
		HDRMODE_FreeSync2_scRGB,
		HDRMODE_Last = HDRMODE_FreeSync2_scRGB
	};

	RENDER_DATA_API eTextureFormat GetHdrModeTextureFormat( eHDRMode hdrMode );

} // end namespace GpuWrapApi

RTTI_DECLARE_ENUM_IN_NAMESPACE( eBufferChunkCategory, GpuWrapApi );
RTTI_DECLARE_ENUM_IN_NAMESPACE( BufferGroup, GpuWrapApi );

#ifdef RTTI_DECLARE_TYPE_NAME_IN_NAMESPACE
	RTTI_DECLARE_ENUM_IN_NAMESPACE( eTextureGroup, GpuWrapApi );
	RTTI_DECLARE_ENUM_IN_NAMESPACE( eTextureFormat, GpuWrapApi );
	RTTI_DECLARE_ENUM_IN_NAMESPACE( eTextureType, GpuWrapApi );
#endif

// --------------------
