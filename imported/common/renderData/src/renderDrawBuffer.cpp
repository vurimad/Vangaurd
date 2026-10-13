/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "renderDrawBuffer.h"
#include "../../../../engine/shaders/include/include_hud.fx"

// #TODO: Vertex/Index data could be allocated from a gpu pool, and then used as in-place buffers when rendering.
// Completely avoid additional copies. Would just need to try to use them in a write-only, in-order fashion, probably
// with writing full cache lines.


RTTI_BEGIN_TYPE_IN_NAMESPACE( CompositionShaderParams, fx );
{
	RTTI_PROPERTY( m_glitchParam ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_glitchParam1 ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_health ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_visionActiveTime ).range( 0.0f, 100.0f ).editable();
	RTTI_PROPERTY( m_uiFactor ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_uiPassthroughFactor ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_mainRenderFactor ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_blurredRenderFactor ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_backgroundTextureFactor ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_backgroundBlurRadius ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_sphericalDistPower ).range( 0.0f, 1.0f ).editable().customEditor( "Vector2Custom:float:%0.3f" );
	RTTI_PROPERTY( m_shadowDistance ).range( 0.0f, 1.0f ).editable().customEditor( "Vector2Custom:float:%0.3f" );
	RTTI_PROPERTY( m_shadowIntensity ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_shadowRadius ).range( 0.0f, 5.0f ).editable();
	RTTI_PROPERTY( m_glowIntensity ).range( 0.0f, 10.0f ).editable();
	RTTI_PROPERTY( m_glowTresholdStart ).range( 0.0f, 4.0f ).editable();
	RTTI_PROPERTY( m_glowTresholdEnd ).range( 0.0f, 4.0f ).editable();
	RTTI_PROPERTY( m_glowBlurRadius ).range( 0.0f, 4.0f ).editable();
	RTTI_PROPERTY( m_vignetteStart ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_vignetteEnd ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_vignetteIntensity ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_blurredRenderSaturation ).range( 0.0f, 2.0f ).editable();
	RTTI_PROPERTY( m_uiSaturation ).range( 0.0f, 2.0f ).editable();
	RTTI_PROPERTY( m_chromaticAberrationStrength ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_uiLayer2Scale ).editable().customEditor( "Vector2Custom:float:%0.3f" );
	RTTI_PROPERTY( m_uiLayer3Scale ).editable().customEditor( "Vector2Custom:float:%0.3f" );
	RTTI_PROPERTY( m_uiLayer4Scale ).editable().customEditor( "Vector2Custom:float:%0.3f" );
	RTTI_PROPERTY( m_uiLayer2Weight ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_uiLayer3Weight ).range( 0.0f, 1.0f ).editable();
	RTTI_PROPERTY( m_uiLayer4Weight ).range( 0.0f, 1.0f ).editable();
}
RTTI_END_TYPE();

namespace fx
{

	CompositionShaderParams::CompositionShaderParams()
	{
		m_glitchParam = 0.0f;
		m_glitchParam1 = 0.0f;
		m_health = 0.0f;
		m_visionActiveTime = 0.0f;
		m_uiFactor = 0.0f;
		m_uiPassthroughFactor = 0.0f;
		m_mainRenderFactor = 0.0f;
		m_blurredRenderFactor = 0.0f;
		m_backgroundTextureFactor = 0.0f;
		m_backgroundBlurRadius = 0.0f;
		m_sphericalDistPower = Vector2( 0.0f, 0.0f );
		m_shadowDistance = Vector2( 0.0f, 0.0f );
		m_shadowIntensity = 0.0f;
		m_shadowRadius = 1.0f;
		m_glowIntensity = 0.5f;
		m_glowTresholdStart = 0.75f;
		m_glowTresholdEnd = 1.0f;
		m_glowBlurRadius = 0.0f;
		m_vignetteStart = 0.0f;
		m_vignetteEnd = 0.0f;
		m_vignetteIntensity = 0.0f;
		m_blurredRenderSaturation = 0.73f;
		m_uiSaturation = 1.0f;
		m_chromaticAberrationStrength = 0.00075f;
		m_uiLayer2Scale = Vector2( -0.005f, -0.0025f );
		m_uiLayer3Scale = m_uiLayer2Scale - Vector2( 0.0058f, 0.0043f );
		m_uiLayer4Scale = m_uiLayer2Scale - Vector2( 0.0076f, 0.0048f );
		m_uiLayer2Weight = 0.08f;
		m_uiLayer3Weight = 0.07f;
		m_uiLayer4Weight = 0.06f;

		m_uiBlacksBarsUV = Vector2::ZEROS();
	}

	CompositionShaderParams::CompositionShaderParams( const CompositionShaderParams &other )
	{
		m_glitchParam = other.m_glitchParam;
		m_glitchParam1 = other.m_glitchParam1;
		m_health = other.m_health;
		m_visionActiveTime = other.m_visionActiveTime;

		m_uiFactor = other.m_uiFactor;
		m_uiPassthroughFactor = other.m_uiPassthroughFactor;
		m_mainRenderFactor = other.m_mainRenderFactor;
		m_blurredRenderFactor = other.m_blurredRenderFactor;
		m_backgroundTextureFactor = other.m_backgroundTextureFactor;

		m_backgroundBlurRadius = other.m_backgroundBlurRadius;

		m_sphericalDistPower = other.m_sphericalDistPower;

		m_shadowDistance = other.m_shadowDistance;
		m_shadowIntensity = other.m_shadowIntensity;
		m_shadowRadius = other.m_shadowRadius;

		m_glowIntensity = other.m_glowIntensity;
		m_glowTresholdStart = other.m_glowTresholdStart;
		m_glowTresholdEnd = other.m_glowTresholdEnd;
		m_glowBlurRadius = other.m_glowBlurRadius;

		m_vignetteStart = other.m_vignetteStart;
		m_vignetteEnd = other.m_vignetteEnd;
		m_vignetteIntensity = other.m_vignetteIntensity;

		m_blurredRenderSaturation = other.m_blurredRenderSaturation;
		m_uiSaturation = other.m_uiSaturation;
		m_chromaticAberrationStrength = other.m_chromaticAberrationStrength;

		m_uiLayer2Scale = other.m_uiLayer2Scale;
		m_uiLayer3Scale = other.m_uiLayer3Scale;
		m_uiLayer4Scale = other.m_uiLayer4Scale;

		m_uiLayer2Weight = other.m_uiLayer2Weight;
		m_uiLayer3Weight = other.m_uiLayer3Weight;
		m_uiLayer4Weight = other.m_uiLayer4Weight;

		m_uiBlacksBarsUV = other.m_uiBlacksBarsUV;
	}

	void CompositionShaderParams::SetFloatParameter( const CName& parameterName, Float value )
	{
		red::DynArray<const rtti::Property*> properties{ red::PoolFrame() };
		const rtti::ClassType* typeDesc = this->GetClass();
		typeDesc->GetProperties( properties );

		for( const rtti::Property* prop : properties )
		{
			const CName propName = prop->GetName();
			if( propName == parameterName )
			{
				void* propData = prop->GetOffsetPtr( this );
				const rtti::IType* propType = prop->GetType();
				const CName propTypeCName = propType->GetName();

				if( propType == GetTypeObject<Float>() )
				{
					propType->Copy( propData, &value );
				}
			}
		}
	}

	void CompositionShaderParams::SetVector2Parameter( const CName& parameterName, Vector2 value )
	{
		red::DynArray<const rtti::Property*> properties{ red::PoolFrame() };
		const rtti::ClassType* typeDesc = this->GetClass();
		typeDesc->GetProperties( properties );

		for( const rtti::Property* prop : properties )
		{
			const CName propName = prop->GetName();
			if( propName == parameterName )
			{
				void* propData = prop->GetOffsetPtr( this );
				const rtti::IType* propType = prop->GetType();
				const CName propTypeCName = propType->GetName();

				if( propType == GetTypeObject<Vector2>() )
				{
					propType->Copy( propData, &value );
				}
			}
		}
	}

	Float CompositionShaderParams::GetFloatParameter( const CName& parameterName )
	{
		red::DynArray<const rtti::Property*> properties{ job::PoolJobScope() };
		const rtti::ClassType* typeDesc = this->GetClass();
		typeDesc->GetProperties( properties );

		for( const rtti::Property* prop : properties )
		{
			const CName propName = prop->GetName();
			if( propName == parameterName )
			{
				void* propData = prop->GetOffsetPtr( this );
				const rtti::IType* propType = prop->GetType();
				const CName propTypeCName = propType->GetName();

				if( propType == GetTypeObject<Float>() )
				{
					auto* floatValue = static_cast< const Float* >( propData );
					return *floatValue;
				}
			}
		}
		return 0.0f;
	}

	Vector2 CompositionShaderParams::GetVector2Parameter( const CName& parameterName )
	{
		red::DynArray<const rtti::Property*> properties{ red::PoolFrame() };
		const rtti::ClassType* typeDesc = this->GetClass();
		typeDesc->GetProperties( properties );

		for( const rtti::Property* prop : properties )
		{
			const CName propName = prop->GetName();
			if( propName == parameterName )
			{
				void* propData = prop->GetOffsetPtr( this );
				const rtti::IType* propType = prop->GetType();
				const CName propTypeCName = propType->GetName();

				if( propType == GetTypeObject<Vector2>() )
				{
					auto* floatValue = static_cast< const Vector2* >( propData );
					return *floatValue;
				}
			}
		}
		return Vector2::ZEROS();
	}

	const red::DynArray<String> CompositionShaderParams::GetParamNames()
	{
		red::DynArray<const rtti::Property*> properties{ red::PoolFrame() };
		red::DynArray<String> paramNames{ red::PoolFrame() };

		const rtti::ClassType* typeDesc = this->GetClass();
		typeDesc->GetProperties( properties );

		for( const rtti::Property* prop : properties )
		{
			const CName propName = prop->GetName();
			const rtti::IType* propType = prop->GetType();
			if( propType == GetTypeObject<Float>() )
			{
				paramNames.PushBack( propName.AsChar() );
			}
		}
		return paramNames;
	}
} // fx

namespace rend
{
	CompositionParams::CompositionParams() = default;

	DrawBuffer::Batch::Batch( const red::memory::Pool& pool )
		: commands( pool )
	{ }

	DrawBuffer::Batch::Batch(Batch&&) = default;
	DrawBuffer::Batch& DrawBuffer::Batch::operator =(Batch&&) = default;

	DrawBufferCommand::DrawBufferCommand()
		: vertexOffset( 0 )
		, indexOffset( 0 )
		, glyphOffset( 0 )
		, vertexCount( 0 )
		, indexCount( 0 )
		, glyphCount( 0 )
	{
		color = math::Vector4( 1.0f, 1.0f, 1.0f, 1.0f );

		for ( Uint32 i = 0; i < MaxMasks; ++i )
		{
			masks[i].rect = math::RectF::EMPTY();
		}

		for ( Uint32 i = 0; i < MaxCustomParams; ++i )
		{
			customParams[ i ] = math::Vector4::ZEROS();
		}
	}

	DrawBuffer::DrawBuffer( const red::memory::Pool& pool, Uint32 width, Uint32 height, CompositionParamsPtr compositionParams )
		: m_currentBatch( nullptr )
		, m_surfaceWidth( width )
		, m_surfaceHeight( height )
		, m_compositionParams( std::move( compositionParams ) )
		, m_vertexData( pool )
		, m_indexData( pool )
		, m_glyphData( pool )
		, m_batches( pool )
	{
		static_assert( sizeof( red::memory::Pool ) == sizeof( m_internalMemoryPool ), "Expected to be able to store a Pool like this... Works in DynamicBuffer, but maybe something changed?" );
		red::Memcpy( &m_internalMemoryPool, &pool, sizeof( red::memory::Pool ) );
	}

	DrawBuffer::~DrawBuffer()
	{
	}

	red::memory::Pool& DrawBuffer::GetInternalMemoryPool() const
	{
		void* poolPtr = const_cast<void**>( &m_internalMemoryPool );
		return *reinterpret_cast<red::memory::Pool*>( poolPtr );
	}

	Bool DrawBuffer::IsEmpty() const
	{
		return m_batches.Empty();
	}

	Uint32 DrawBuffer::GetNumBatches() const
	{
		return m_batches.Size();
	}

	const DrawBuffer::Batch& DrawBuffer::GetBatch( Uint32 index ) const
	{
		return m_batches[index];
	}

	void DrawBuffer::NewBatch( const TRenderPtr<IRenderResource>& renderTarget, const TRenderPtr<IRenderResource>& resolvedRenderTarget, const Rect& viewport )
	{
		// If the last batch didn't actually do anything, then we can reuse it.
		Bool canReuseBatch = false;
		if ( !m_batches.Empty() )
		{
			if ( !m_batches.Back().doClear && m_batches.Back().commands.Empty() )
			{
				canReuseBatch = true;
			}
		}

		if ( !canReuseBatch )
		{
			m_batches.EmplaceBack( GetInternalMemoryPool() );
		}
		m_currentBatch = &m_batches.Back();

		m_currentBatch->renderTarget = renderTarget;
		m_currentBatch->resolvedRenderTarget = resolvedRenderTarget;
		m_currentBatch->viewport = viewport;
		m_currentBatch->doClear = false;
	}

	void DrawBuffer::Push( const ClearCommand& command )
	{
		RED_ASSERT(m_currentBatch != nullptr, "Cannot push a command without an active batch.");
		RED_ASSERT(m_currentBatch->commands.Empty(), "Pushing a clear command, but current batch already has draws! Seem to have some wasted effort here.");
		m_currentBatch->clearColor = command.clearColor;
		m_currentBatch->doClear = true;
	}

	void DrawBuffer::Push( DrawBufferCommand&& command )
	{
		RED_ASSERT(m_currentBatch != nullptr, "Cannot push a draw command without an active batch.");
		m_currentBatch->commands.PushBack( std::move(command) );
	}

	DrawBuffer::Allocation DrawBuffer::AllocateVertices( Uint32 bytes )
	{
		Allocation result;
		result.offset = m_vertexData.Size();
		m_vertexData.Grow( bytes );
		result.ptr = &m_vertexData[result.offset];
		return result;
	}

	DrawBuffer::Allocation DrawBuffer::AllocateIndices( Uint32 bytes )
	{
		Allocation result;
		result.offset = m_indexData.Size();
		m_indexData.Grow( bytes );
		result.ptr = &m_indexData[result.offset];
		return result;
	}

	DrawBuffer::Allocation DrawBuffer::AllocateGlyphs( Uint32 bytes )
	{
		Allocation result;
		result.offset = m_glyphData.Size();
		m_glyphData.Grow( bytes );
		result.ptr = &m_glyphData[ result.offset ];
		return result;
	}

	const void* DrawBuffer::GetVerticesPtr( Uint32 offset ) const
	{
		RED_ASSERT(offset < m_vertexData.Size(), "Invalid vertices offset");
		return &m_vertexData[offset];
	}

	const void* DrawBuffer::GetIndicesPtr( Uint32 offset ) const
	{
		RED_ASSERT(offset < m_indexData.Size(), "Invalid indices offset");
		return &m_indexData[offset];
	}

	const void* DrawBuffer::GetGlyphsPtr( Uint32 offset ) const
	{
		RED_ASSERT( offset < m_glyphData.Size(), "Invalid glyphs offset" );
		return &m_glyphData[ offset ];
	}

}
