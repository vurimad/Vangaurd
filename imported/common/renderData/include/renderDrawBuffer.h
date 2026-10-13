/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "materialFeatureFlags.h"
#include "renderDrawCommon.h"
#include "fontStyle.h"
#include "../../renderData/include/renderObjectPtr.h"

enum EMaterialVertexFactory : Uint8;

namespace fx
{
	struct CompositionShaderParams;
}

namespace rend
{
	class Font;

	struct DrawBufferMaskInfo
	{
		TRenderPtr<IRenderResource>	texture;
		math::RectF rect;			// viewport-space mask rectangle
		math::RectF texCoords;		// mask texture source coordinates
		Float angle;				// mask rotation angle in radians (rotates around upper left corner)
		Float opactity;				// controls how transparent mask is
		Bool invert;				// invert mask value in the shader (by default disabled - value 0.0 makes the mask non-transparent)

		RED_FORCE_INLINE DrawBufferMaskInfo()
		{
			rect = math::RectF::EMPTY();
			texCoords = math::RectF( 0.0f, 1.0f, 0.0f, 1.0f );
			angle = 0.0f;
			opactity = 0.0f;
			invert = false;
		}
	};

	struct DrawBufferEffectInfo
	{
		Bool enabled;
		TRenderPtr<IRenderResource>	texture;
		math::RectF rect;			// viewport-space mask rectangle
		math::RectF texCoords;		// mask texture source coordinates
		Vector4 params;

		RED_FORCE_INLINE DrawBufferEffectInfo()
		{
			rect = math::RectF::EMPTY();
			texCoords = math::RectF( 0.0f, 1.0f, 0.0f, 1.0f );
			params = Vector4::ZEROS();
			enabled = false;
		}
	};

	// #TODO: Pack this. It's way bigger than necessary.
	struct RENDER_DATA_API DrawBufferCommand
	{
		static constexpr Uint32 MaxMasks = 3;
		static constexpr Uint32 MaxCustomParams = 3;

		DrawBufferCommand();

		DrawType drawType;
		PrimitiveType primitiveType;

		Uint32 vertexOffset;
		Uint32 indexOffset;
		Uint32 glyphOffset;

		Uint32 vertexCount;
		Uint32 indexCount;
		Uint32 glyphCount;

		FeatureFlagsMask featureFlags;

		THandle< Font > font;
		FontStyle fontStyle;

		Matrix transform;

		TRenderPtr<IRenderResource>	texture;

		math::Vector4 color;				// linear space,final color is texture*vertex*color
		math::Color outlineColor;			// for font only

		// If provided, override default material. Depending on the material, the above parameters might end up
		// being ignored.
		TRenderPtr<IRenderResource> material;
		TRenderPtr<IRenderResource> materialParameters;

		DrawBufferMaskInfo masks[ MaxMasks ];
		DrawBufferEffectInfo effectInfo;

		math::Vector4 customParams[ MaxCustomParams ];

		Uint32 GetNumElements() const
		{
			switch ( drawType )
			{
			case DrawType::Draw:		return vertexCount;
			case DrawType::DrawIndexed:	return indexCount;
			case DrawType::DrawText:	return glyphCount;
			default:
				RED_HALT( "Invalid draw type %d", (Uint32)drawType );
			}
			return 0;
		}
	};

	struct RENDER_DATA_API CompositionParams
	{
		RED_USE_MEMORY_POOL( PoolRenderingUI );

		CompositionParams();

		TRenderPtr<IRenderResource>	backgroundTexture;
		TRenderPtr<IRenderResource>	previousFrameTexture;

		red::UniquePtr< fx::CompositionShaderParams > shaderParams;
	};

	using CompositionParamsPtr = red::UniquePtr< CompositionParams >;

	class RENDER_DATA_API DrawBuffer : public red::NonCopyable
	{
		RED_USE_MEMORY_POOL( PoolRenderingUI );

	public:

		struct Allocation
		{
			void* ptr;					// Valid until next allocate of same type.
			Uint32 offset;				// Valid for lifetime.
		};


		struct ClearCommand
		{
			Color clearColor;
		};

		struct RENDER_DATA_API Batch : red::NonCopyable
		{
			Batch( const red::memory::Pool& pool );
			Batch(Batch&&);
			Batch& operator =(Batch&&);

			// null when rendering as composition
			TRenderPtr<IRenderResource> renderTarget;
			TRenderPtr<IRenderResource> resolvedRenderTarget; // same as 'renderTarget' when MSAA is disabled for the target
			Rect viewport;

			Color clearColor;
			Bool doClear;

			red::DynArray<DrawBufferCommand> commands;
		};

	public:
		DrawBuffer( const red::memory::Pool& pool, Uint32 width = 0, Uint32 height = 0, CompositionParamsPtr compositionParams = nullptr );
		~DrawBuffer();

		Bool IsEmpty() const;

		RED_FORCE_INLINE Uint32 GetSurfaceWidth() const { return m_surfaceWidth; }
		RED_FORCE_INLINE Uint32 GetSurfaceHeight() const { return m_surfaceHeight; }
		RED_FORCE_INLINE const CompositionParams* GetCompositionParams() const { return m_compositionParams.Get(); }

		void NewBatch( const TRenderPtr<IRenderResource>& renderTarget, const TRenderPtr<IRenderResource>& resolvedRenderTarget, const Rect& viewport );

		// Push a command into the current batch.
		void Push( const ClearCommand& command );
		void Push( DrawBufferCommand&& command );

		Allocation AllocateVertices( Uint32 bytes );
		Allocation AllocateIndices( Uint32 bytes );
		Allocation AllocateGlyphs( Uint32 bytes );

		Uint32 GetNumBatches() const;
		const Batch& GetBatch( Uint32 index ) const;

		const void* GetVerticesPtr( Uint32 offset ) const;
		const void* GetIndicesPtr( Uint32 offset ) const;
		const void* GetGlyphsPtr( Uint32 offset ) const;

	private:
		
		red::memory::Pool& GetInternalMemoryPool() const;

		void* m_internalMemoryPool;

		red::DynArray<Uint8> m_vertexData;
		red::DynArray<Uint8> m_indexData;
		red::DynArray<Uint8> m_glyphData;
		red::DynArray<Batch> m_batches;

		Batch* m_currentBatch;

		Uint32 m_surfaceWidth;
		Uint32 m_surfaceHeight;

		// used only for full-screen composition
		CompositionParamsPtr m_compositionParams;
	};

}
