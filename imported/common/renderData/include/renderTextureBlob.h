/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/deferredDataBuffer.h"
#include "renderResourceBlob.h"
#include "renderingMemory.h"
#include "renderSimpleTypes.h"

enum ECookingPlatform : Uint8;

namespace rend
{
	static const Uint32 st_MaxMipMapsCount = 13;

	struct HistogramBias
	{
		RTTI_DECLARE_TYPE( HistogramBias );

		Vector3			m_mulCoef;
		Vector3			m_addCoef;

		HistogramBias()
			: m_mulCoef( Vector3( 1,1,1 ) )
			, m_addCoef( Vector3( 0,0,0 ) )
		{}

		HistogramBias( Vector3 mulCoef, Vector3 addCoef )
			: m_mulCoef( mulCoef )
			, m_addCoef( addCoef )
		{}
	};

	struct RenderTextureBlobSizeInfo
	{
		RTTI_DECLARE_TYPE( RenderTextureBlobSizeInfo );

		Uint16			m_width;		 // surface width (in pixels)
		Uint16			m_height;		 // surface height (in pixels)
		Uint16			m_depth;		 // depth of a volume 3D texture (in pixels), otherwise unused

		RenderTextureBlobSizeInfo()
			: m_width( 0 )
			, m_height( 0 )
			, m_depth( 1 )
		{}

	};
	typedef RenderTextureBlobSizeInfo SizeInfo;

	struct RenderTextureBlobTextureInfo
	{
		RTTI_DECLARE_TYPE( RenderTextureBlobTextureInfo );

		GpuWrapApi::eTextureType	m_type;							 // texture type

		Uint32						m_textureDataSize;	// size of the whole texture data
		Uint32						m_sliceSize;		// size of a single texture in the uniform array
		Uint32						m_dataAlignment;	// data alignment
		Uint16						m_sliceCount;		// number of slices (elements in a texture along with its complete mipmap chain)
		Uint8						m_mipCount;			// number of mip map levels, otherwise unused

		RenderTextureBlobTextureInfo()
			: m_type( GpuWrapApi::TEXTYPE_2D )
			, m_textureDataSize( 0 )
			, m_sliceSize( 0 )
			, m_dataAlignment( 0 )
			, m_sliceCount( 0 )
			, m_mipCount( 0 )
		{}

	};
	typedef RenderTextureBlobTextureInfo TextureInfo;

	struct RenderTextureBlobMemoryLayout
	{
		RTTI_DECLARE_TYPE( RenderTextureBlobMemoryLayout );

		Uint32			m_rowPitch;		 // size of the mip map level row pixel data in bytes
		Uint32			m_slicePitch;	 // size of the mip map level slice texel data in bytes (3D textures only)

		RenderTextureBlobMemoryLayout()
			: m_rowPitch( 0 )
			, m_slicePitch( 0 )
		{}
	};
	typedef RenderTextureBlobMemoryLayout MemoryLayout;

	struct RenderTextureBlobPlacement
	{
		RTTI_DECLARE_TYPE( RenderTextureBlobPlacement );

		Uint32			m_offset;	// offset in bytes from the begining of slice mipmap data
		Uint32			m_size;		// size in bytes

		RenderTextureBlobPlacement()
			: m_offset( 0 )
			, m_size( 0 )
		{}
	};
	typedef RenderTextureBlobPlacement Placement;

	struct RenderTextureBlobMipMapInfo
	{
		RTTI_DECLARE_TYPE( RenderTextureBlobMipMapInfo );

		MemoryLayout	 m_layout;		// mip map level memory layout
		Placement		 m_placement;	// mip map level placement within a blob
	};
	typedef RenderTextureBlobMipMapInfo MipMapInfo;
	typedef red::DynArray< MipMapInfo > TMipMapInfoArray;

	typedef red::DynArray< HistogramBias > THistogramBiasArray;
	struct RenderTextureBlobHeader
	{
		RTTI_DECLARE_TYPE( RenderTextureBlobHeader );

		Uint32				 m_version;		 // version of the format
		SizeInfo			 m_sizeInfo;	 // size info of the texture resource
		TextureInfo			 m_textureInfo;  // texture info of the texture resource
		TMipMapInfoArray	 m_mipMapInfo{ rend::PoolTexture() };	 // mip map data info template
		THistogramBiasArray  m_histogramData{ rend::PoolTexture() };// color range reconstruction params
		Uint32				 m_flags;		 // various flags, currently RED_FLAG(0) = cooked/optimized data
	};

	enum ERenderTextureBlobHeaderFlags
	{
		RTBHF_Optimized = RED_FLAG(0),
		RTBHF_Tiled = RED_FLAG(1),
	};
	

	struct RenderTextureBlobSetup
	{
		Uint32 alignment;
		serialization::DeferredDataBufferFlags flag;
	};

	class RENDER_DATA_API IRenderTextureBlob : public IRenderResourceBlob
	{
		RTTI_DECLARE_TYPE( IRenderTextureBlob );
		RED_BASE_CLASS( IRenderResourceBlob )

	public:
		struct RenderTextureBlobVersions
		{
			enum : Uint32
			{
				VER0,
				VER1,
				VER2_FixedConsoleData,

				VER_MAX,

				VER_Current = VER_MAX-1,

				// We can create textures without fixed console data, but it might not look correct.
				VER_MinSupported = VER1,
			};
		};

	public:
		typedef RenderTextureBlobHeader Header;

		static red::UniquePtr< IRenderTextureBlob > CreateRenderTextureBlob( ECookingPlatform platform );

#ifndef NO_EDITOR
		static red::UniquePtr< IRenderTextureBlob > CreateRenderTextureBlobStreamable(const IRenderTextureBlob& srcBlob);
#endif

	protected:
		explicit IRenderTextureBlob( const RenderTextureBlobSetup& setup );

	private:
		// #fixme: remove when RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE, but need to resave data first
		// otherwise will fail to create an abstract class for current data
		IRenderTextureBlob();

	public:
		~IRenderTextureBlob();

		RED_INLINE virtual ERenderResourceType GetType() const override { return RRT_Texture; }

		Bool IsOptimized() const;

		Bool IsTiled() const;

		virtual Uint32 GetVersion() const override;

		virtual Bool IsVersionCurrent() const override;

		virtual Bool IsVersionValid() const override;

		virtual Uint32 GetDataSize() const override;
	
		Header m_header;
		
		using TextureDataBuffer = serialization::DeferredDataBuffer;
		TextureDataBuffer m_textureData;
	};

	class RENDER_DATA_API RenderTextureResource
	{
		RTTI_DECLARE_TYPE( RenderTextureResource );

	public:
		RenderTextureResource();
		~RenderTextureResource();

		void InitRenderResourceBlob( ECookingPlatform platform, const THandle< IRenderTextureBlob >& blob );
		const THandle< IRenderResourceBlob >& GetCookingPlatfromResourceBlob( ECookingPlatform platform ) const;
		void Clear();

		const THandle< IRenderResourceBlob >& GetRenderResourceBlob() const;

	private:
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_LINUX )
		THandle< IRenderResourceBlob > m_renderResourceBlobPC;
#endif
#if defined( RED_PLATFORM_DURANGO ) || !defined( NO_TEXTURE_EDITING )
		THandle< IRenderResourceBlob > m_renderResourceBlobXboxOne;
#endif
#if defined( RED_PLATFORM_ORBIS ) || !defined( NO_TEXTURE_EDITING )
		THandle< IRenderResourceBlob > m_renderResourceBlobPS4;
#endif
	};

}//rnd

struct ISkinProfileData
{
	Float kernelSize;
	Color diffuse;
	Color falloff;

	ISkinProfileData()
		: kernelSize( 1.2f )
		, diffuse( Color( 122, 104, 71 ) )
		, falloff( Color( 255,  94, 76 ) )
	{}

	void Invalidate()
	{
		kernelSize = 0.f;
		diffuse = Color::BLACK();
		falloff = Color::BLACK();
	}
};







