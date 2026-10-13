/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/deferredDataBuffer.h"
#include "renderResourceBlob.h"
#include "renderingMemory.h"

enum ECookingPlatform : Uint8;

namespace rend
{

	enum MultilayerMaskBlobHeaderFlags
	{
		MLMask_Tiled				= RED_FLAG(0),		// Is the data in a pre-tiled layout for the cooked platform?
		MLMask_UseMultiresolution	= RED_FLAG(1),		// If set, mlmask supports different resolution for each layer.
		MLMask_Uncompressed			= RED_FLAG(2),		// If set, the mlmask atlas is uncompressed
	};


	struct RenderMultilayerMaskBlobVersions {
		enum : Uint32
		{
			VER0_Invalid,
			VER1_Initial,
			VER2_LayerMasks,
			VER3_NonPow2,
		};
	};


	struct RENDER_DATA_API RenderMultilayerMaskBlobHeader
	{
		RTTI_DECLARE_TYPE( RenderMultilayerMaskBlobHeader );

		Uint32		m_version;

		// Size of the atlas texture
		Uint32		m_atlasWidth;
		Uint32		m_atlasHeight;

		// Number of layers in the mask
		Uint32		m_numLayers;

		// Base size of full resolution masks
		Uint32		m_maskWidth;
		Uint32		m_maskHeight;

		// Base size of reduced resolution masks. Unused if not Multiresolution.
		Uint32		m_maskWidthLow;
		Uint32		m_maskHeightLow;

		// size of a tile from the mask.
		// If Multiresoltion, tiles in atlas are 2px bigger; else 1px bigger.
		Uint32		m_maskTileSize;

		// Flags from MultilayerMaskBlobHeaderFlags
		Uint32		m_flags;

		RenderMultilayerMaskBlobHeader();

		RED_INLINE Bool IsMultiresolution() const { return (m_flags & MLMask_UseMultiresolution) != 0; }
		RED_INLINE Bool IsUncompressed() const { return (m_flags & MLMask_Uncompressed) != 0; }
	};


	struct RenderMultilayerMaskBlobSetup
	{
		Uint32 alignment;
		serialization::DeferredDataBufferFlags flag;
	};

	class RENDER_DATA_API RenderMultilayerMaskBlob : public IRenderResourceBlob
	{
		RTTI_DECLARE_TYPE( RenderMultilayerMaskBlob );
		RED_BASE_CLASS( IRenderResourceBlob );

	public:
		typedef RenderMultilayerMaskBlobHeader Header;

		static red::UniquePtr< RenderMultilayerMaskBlob > CreateRenderMultilayerMaskBlobPC();
		static red::UniquePtr< RenderMultilayerMaskBlob > CreateRenderMultilayerMaskBlobXboxOne();
		static red::UniquePtr< RenderMultilayerMaskBlob > CreateRenderMultilayerMaskBlobPS4();

	protected:
		explicit RenderMultilayerMaskBlob( const RenderMultilayerMaskBlobSetup& setup );

	public:
		~RenderMultilayerMaskBlob();

		// Minimum supported version
		static constexpr Uint32 MIN_VERSION = RenderMultilayerMaskBlobVersions::VER1_Initial;
		// Latest version
		static constexpr Uint32 CURRENT_VERSION = RenderMultilayerMaskBlobVersions::VER3_NonPow2;

		RED_INLINE virtual ERenderResourceType GetType() const override { return RRT_MultilayerMask; }

		RED_INLINE Bool IsTiled() const { return (m_header.m_flags & MLMask_Tiled) != 0; }

		virtual Uint32 GetVersion() const override;
		virtual Bool IsVersionCurrent() const override;
		virtual Bool IsVersionValid() const override;

		virtual Uint32 GetDataSize() const override;

		Header m_header;
		
		using TextureDataBuffer = serialization::DeferredDataBuffer;
		using BufferDataBuffer = serialization::DeferredDataBuffer;

		TextureDataBuffer m_atlasData;
		BufferDataBuffer m_tilesData;
	};


	class RENDER_DATA_API RenderMultilayerMaskResource
	{
		RTTI_DECLARE_TYPE( RenderMultilayerMaskResource );

	public:
		RenderMultilayerMaskResource();
		~RenderMultilayerMaskResource();

		void InitRenderResourceBlob( ECookingPlatform platform, const THandle< RenderMultilayerMaskBlob >& blob );
		const THandle< IRenderResourceBlob >& GetCookingPlatfromResourceBlob( ECookingPlatform platform ) const;
		void Clear();

		const THandle< IRenderResourceBlob >& GetRenderResourceBlob() const;

	private:
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_LINUX)
		THandle< IRenderResourceBlob > m_renderResourceBlobPC;
#endif
#if defined( RED_PLATFORM_DURANGO ) || !defined( NO_TEXTURE_EDITING )
		THandle< IRenderResourceBlob > m_renderResourceBlobXboxOne;
#endif
#if defined( RED_PLATFORM_ORBIS ) || !defined( NO_TEXTURE_EDITING )
		THandle< IRenderResourceBlob > m_renderResourceBlobPS4;
#endif
	};

}
