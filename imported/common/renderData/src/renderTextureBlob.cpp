/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderTextureBlob.h"
#include "../../redReflection/include/util.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( HistogramBias, rend );
{
	RTTI_PROPERTY( m_mulCoef );
	RTTI_PROPERTY( m_addCoef );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureBlobMemoryLayout, rend );
{
	RTTI_PROPERTY( m_rowPitch );
	RTTI_PROPERTY( m_slicePitch );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureBlobPlacement, rend );
{
	RTTI_PROPERTY( m_offset );
	RTTI_PROPERTY( m_size );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureBlobSizeInfo, rend );
{
	RTTI_PROPERTY( m_width );
	RTTI_PROPERTY( m_height );
	RTTI_PROPERTY( m_depth );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureBlobTextureInfo, rend );
{
	RTTI_PROPERTY( m_type );
	RTTI_PROPERTY( m_textureDataSize );
	RTTI_PROPERTY( m_sliceSize );
	RTTI_PROPERTY( m_dataAlignment );
	RTTI_PROPERTY( m_sliceCount );
	RTTI_PROPERTY( m_mipCount );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureBlobMipMapInfo, rend );
{
	RTTI_PROPERTY( m_layout );
	RTTI_PROPERTY( m_placement );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureBlobHeader, rend );
{
	RTTI_PROPERTY( m_version );
	RTTI_PROPERTY( m_sizeInfo );
	RTTI_PROPERTY( m_textureInfo );
	RTTI_PROPERTY( m_mipMapInfo );
	RTTI_PROPERTY( m_histogramData );
	RTTI_PROPERTY( m_flags );
}
RTTI_END_TYPE();

//RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IRenderTextureBlob, rend );
RTTI_BEGIN_TYPE_IN_NAMESPACE( IRenderTextureBlob, rend );
{
	RTTI_PARENT_TYPE( IRenderResourceBlob );
	RTTI_PROPERTY( m_header );
	RTTI_PROPERTY( m_textureData );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureResource, rend );
{
#if defined( RED_PLATFORM_WINPC )
	RTTI_PROPERTY( m_renderResourceBlobPC );
#endif
#if defined( RED_PLATFORM_DURANGO ) || !defined( NO_TEXTURE_EDITING )
	RTTI_PROPERTY( m_renderResourceBlobXboxOne );
#endif
#if defined( RED_PLATFORM_ORBIS ) || !defined( NO_TEXTURE_EDITING )
	RTTI_PROPERTY( m_renderResourceBlobPS4 );
#endif
}
RTTI_END_TYPE();

namespace rend
{
	class RenderTextureBlobStreamable final : public IRenderTextureBlob
	{
		RTTI_DECLARE_TYPE(RenderTextureBlobStreamable);
		RED_BASE_CLASS(IRenderTextureBlob);

		static const Uint32 c_memoryAlign = 16;

	public:
		RenderTextureBlobStreamable()
			: TBaseClass(RenderTextureBlobSetup{ c_memoryAlign, serialization::DDBF_None })
		{
		}

		virtual serialization::LoadingToken StartStreaming(io::EAsyncPriority priority) override
		{
			return m_textureData.IssueLoadingJobs(priority);
		}
	};

	class RenderTextureBlobPC final : public IRenderTextureBlob
	{
		RTTI_DECLARE_TYPE( RenderTextureBlobPC );
		RED_BASE_CLASS( IRenderTextureBlob );

		static const Uint32 c_memoryAlign = 16;

	public:
		RenderTextureBlobPC()
			: TBaseClass( RenderTextureBlobSetup{ c_memoryAlign, serialization::DDBF_AutoLoadPC } )
		{
		}
	};

	class RenderTextureBlobXboxOne final : public IRenderTextureBlob
	{
		RTTI_DECLARE_TYPE( RenderTextureBlobXboxOne );
		RED_BASE_CLASS( IRenderTextureBlob );
		RED_USE_MEMORY_POOL( PoolTexture );

		// #tbd: not loaded in place in GPU memory yet
		static const Uint32 c_memoryAlign = 16;

	public:
		RenderTextureBlobXboxOne()
			: TBaseClass( RenderTextureBlobSetup{ c_memoryAlign, serialization::DDBF_AutoLoadXboxOne } )
		{
		}
	};

	class RenderTextureBlobPS4 final : public IRenderTextureBlob
	{
		RTTI_DECLARE_TYPE( RenderTextureBlobPS4 );
		RED_BASE_CLASS( IRenderTextureBlob );
		RED_USE_MEMORY_POOL( PoolTexture );

		// #tbd: not loaded in place in GPU memory yet
		static const Uint32 c_memoryAlign = 16;

	public:
		RenderTextureBlobPS4()
			: TBaseClass( RenderTextureBlobSetup{ c_memoryAlign, serialization::DDBF_AutoLoadPS4 } )
		{
		}
	};

#ifndef NO_EDITOR
	red::UniquePtr< rend::IRenderTextureBlob > rend::IRenderTextureBlob::CreateRenderTextureBlobStreamable(const IRenderTextureBlob& srcBlob)
	{
		auto ret = red::UniquePtr< rend::IRenderTextureBlob >(RED_NEW(RenderTextureBlobStreamable));
		ret->m_header = srcBlob.m_header;
		ret->m_textureData.Editor_SetContentDirect(srcBlob.m_textureData.Editor_LoadContentDirect());

		return ret;
	}
#endif

	red::UniquePtr< rend::IRenderTextureBlob > rend::IRenderTextureBlob::CreateRenderTextureBlob( ECookingPlatform platform )
	{
		switch( platform )
		{
		case PLATFORM_WindowsServer:
		case PLATFORM_PC:		return red::UniquePtr< rend::IRenderTextureBlob >( RED_NEW( RenderTextureBlobPC ) );
		case PLATFORM_XboxOne:	return red::UniquePtr< rend::IRenderTextureBlob >( RED_NEW( RenderTextureBlobXboxOne ) );
		case PLATFORM_PS4:		return red::UniquePtr< rend::IRenderTextureBlob >( RED_NEW( RenderTextureBlobPS4 ) );
		default:
			RED_LOG_WARNING( "TexCompiler: Could not select appropriate texture blob compile function" );
		}

		return {};
	}
	

	// don't memset header; has some members with ctors and will mess up differential serialization!
	rend::IRenderTextureBlob::IRenderTextureBlob()
		: m_textureData()
		, m_header()
	{
	}

	namespace helper
	{
		static Uint8 GetRenderTextureBlobFlags(const RenderTextureBlobSetup& setup)
		{
			Uint8 flags = 0;
			flags |= setup.flag;
			return flags;
		}
	}

	// Use PoolDefault here so it will be overridden during OnSerialize with parent objects memory pool. E.g., PoolTexture.
	rend::IRenderTextureBlob::IRenderTextureBlob( const RenderTextureBlobSetup& setup )
		: m_textureData( rend::PoolRenderBlobsBuffers(), setup.alignment, helper::GetRenderTextureBlobFlags( setup ) )
		, m_header()
	{
	}
	
	rend::IRenderTextureBlob::~IRenderTextureBlob()
	{
	}

	Bool rend::IRenderTextureBlob::IsOptimized() const
	{
		return (m_header.m_flags & RTBHF_Optimized) != 0;
	}

	Bool rend::IRenderTextureBlob::IsTiled() const
	{
		return (m_header.m_flags & RTBHF_Tiled) != 0;
	}

	Uint32 rend::IRenderTextureBlob::GetVersion() const
	{
		return m_header.m_version;
	}


	Bool rend::IRenderTextureBlob::IsVersionCurrent() const
	{
		return m_header.m_version == RenderTextureBlobVersions::VER_Current;
	}

	Bool rend::IRenderTextureBlob::IsVersionValid() const
	{
		return m_header.m_version >= RenderTextureBlobVersions::VER_MinSupported && m_header.m_version <= RenderTextureBlobVersions::VER_Current;
	}

	Uint32 rend::IRenderTextureBlob::GetDataSize() const
	{
		return m_textureData.GetSize();
	}

	RenderTextureResource::RenderTextureResource()
	{
	}

	RenderTextureResource::~RenderTextureResource()
	{
	}

	void RenderTextureResource::InitRenderResourceBlob( ECookingPlatform platform, const THandle< IRenderTextureBlob >& blob )
	{
		RED_FATAL_ASSERT( blob );

#ifndef NO_TEXTURE_EDITING
		switch ( platform )
		{
		case PLATFORM_PC:
		case PLATFORM_WindowsServer:
			m_renderResourceBlobPC = blob;
			break;
		case PLATFORM_PS4:
			m_renderResourceBlobPS4 = blob;
			break;
		case PLATFORM_XboxOne:
			m_renderResourceBlobXboxOne = blob;
			break;
		default:
			RED_FATAL( "Unexpected platform %u", platform );
			break;
		}
#endif
	}

	const THandle< IRenderResourceBlob >& RenderTextureResource::GetCookingPlatfromResourceBlob( ECookingPlatform platform ) const
	{
#ifndef NO_TEXTURE_EDITING
		switch ( platform )
		{
		case PLATFORM_PC:
		case PLATFORM_WindowsServer:
			return m_renderResourceBlobPC;
		case PLATFORM_PS4:
			return m_renderResourceBlobPS4;
		case PLATFORM_XboxOne:
			return m_renderResourceBlobXboxOne;
		default:
			RED_FATAL( "Unexpected platform %u", platform );
			break;
		}

#endif
		static THandle< IRenderResourceBlob > nullResourceBlob;
		return nullResourceBlob;
	}

	void RenderTextureResource::Clear()
	{
#ifndef NO_TEXTURE_EDITING
		m_renderResourceBlobPC.Reset();
		m_renderResourceBlobPS4.Reset();
		m_renderResourceBlobXboxOne.Reset();
#endif
	}

	const THandle< IRenderResourceBlob >& RenderTextureResource::GetRenderResourceBlob() const
	{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_LINUX )
		return m_renderResourceBlobPC;
#elif defined( RED_PLATFORM_ORBIS )
		return m_renderResourceBlobPS4;
#elif defined( RED_PLATFORM_DURANGO )
		return m_renderResourceBlobXboxOne;
#else
# error Undefined platform!
		return nullptr;
#endif
	}

} // rend

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureBlobStreamable, rend );
	RTTI_PARENT_TYPE( rend::IRenderTextureBlob );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureBlobPC, rend );
	RTTI_PARENT_TYPE( rend::IRenderTextureBlob );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureBlobXboxOne, rend );
	RTTI_PARENT_TYPE( rend::IRenderTextureBlob );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderTextureBlobPS4, rend );
	RTTI_PARENT_TYPE( rend::IRenderTextureBlob );
RTTI_END_TYPE();
