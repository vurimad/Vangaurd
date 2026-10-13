/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderMultilayerMaskBlob.h"
#include "renderMultilayer.h"
#include "../../redReflection/include/util.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderMultilayerMaskBlobHeader, rend );
{
	RTTI_PROPERTY( m_version );
	RTTI_PROPERTY( m_atlasWidth );
	RTTI_PROPERTY( m_atlasHeight );
	RTTI_PROPERTY( m_numLayers );
	RTTI_PROPERTY( m_maskWidth );
	RTTI_PROPERTY( m_maskHeight );
	RTTI_PROPERTY( m_maskWidthLow );
	RTTI_PROPERTY( m_maskHeightLow );
	RTTI_PROPERTY( m_maskTileSize );
	RTTI_PROPERTY( m_flags );
}
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( RenderMultilayerMaskBlob, rend );
{
	RTTI_PARENT_TYPE( IRenderResourceBlob );
	RTTI_PROPERTY( m_header );
	RTTI_PROPERTY( m_atlasData );
	RTTI_PROPERTY( m_tilesData );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderMultilayerMaskResource, rend );
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

	RenderMultilayerMaskBlobHeader::RenderMultilayerMaskBlobHeader()
		: m_version( RenderMultilayerMaskBlobVersions::VER0_Invalid )
		, m_flags( 0 )
	{
	}


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	class RenderMultilayerMaskBlobPC final : public RenderMultilayerMaskBlob
	{
		RTTI_DECLARE_TYPE( RenderMultilayerMaskBlobPC );
		RED_BASE_CLASS( RenderMultilayerMaskBlob );

		static const Uint32 c_memoryAlign = 16;

	public:
		RenderMultilayerMaskBlobPC()
			: TBaseClass( RenderMultilayerMaskBlobSetup{ c_memoryAlign, serialization::DDBF_AutoLoadPC } )
		{
		}
	};

	class RenderMultilayerMaskBlobXboxOne final : public RenderMultilayerMaskBlob
	{
		RTTI_DECLARE_TYPE( RenderMultilayerMaskBlobXboxOne );
		RED_BASE_CLASS( RenderMultilayerMaskBlob );

		// #tbd: not loaded in place in GPU memory yet
		static const Uint32 c_memoryAlign = 16;

	public:
		RenderMultilayerMaskBlobXboxOne()
			: TBaseClass( RenderMultilayerMaskBlobSetup{ c_memoryAlign, serialization::DDBF_AutoLoadXboxOne } )
		{
		}
	};

	class RenderMultilayerMaskBlobPS4 final : public RenderMultilayerMaskBlob
	{
		RTTI_DECLARE_TYPE( RenderMultilayerMaskBlobPS4 );
		RED_BASE_CLASS( RenderMultilayerMaskBlob );

		// #tbd: not loaded in place in GPU memory yet
		static const Uint32 c_memoryAlign = 16;

	public:
		RenderMultilayerMaskBlobPS4()
			: TBaseClass( RenderMultilayerMaskBlobSetup{ c_memoryAlign, serialization::DDBF_AutoLoadPS4 } )
		{
		}
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	red::UniquePtr< RenderMultilayerMaskBlob > RenderMultilayerMaskBlob::CreateRenderMultilayerMaskBlobPC()
	{
		return red::UniquePtr< RenderMultilayerMaskBlob >( RED_NEW( RenderMultilayerMaskBlobPC ) );
	}

	red::UniquePtr< RenderMultilayerMaskBlob > RenderMultilayerMaskBlob::CreateRenderMultilayerMaskBlobXboxOne()
	{
		return red::UniquePtr< RenderMultilayerMaskBlob >( RED_NEW( RenderMultilayerMaskBlobXboxOne ) );
	}

	red::UniquePtr< RenderMultilayerMaskBlob > RenderMultilayerMaskBlob::CreateRenderMultilayerMaskBlobPS4()
	{
		return red::UniquePtr< RenderMultilayerMaskBlob >( RED_NEW( RenderMultilayerMaskBlobPS4 ) );
	}

	namespace helper
	{
		static Uint8 GetRenderMultilayerMaskBlobFlags(const RenderMultilayerMaskBlobSetup& setup)
		{
			Uint8 flags = 0;
			flags |= setup.flag;
			return flags;
		}
	}

	// Use PoolDefault here so it will be overridden during OnSerialize with parent objects memory pool. E.g., PoolRenderBlobs.
	RenderMultilayerMaskBlob::RenderMultilayerMaskBlob( const RenderMultilayerMaskBlobSetup& setup )
		: m_atlasData( rend::PoolRenderBlobs(), setup.alignment, helper::GetRenderMultilayerMaskBlobFlags( setup ) )
		, m_tilesData( rend::PoolRenderBlobs(), 16, helper::GetRenderMultilayerMaskBlobFlags(setup) )
		, m_header()
	{
	}

	RenderMultilayerMaskBlob::~RenderMultilayerMaskBlob() = default;


	Uint32 RenderMultilayerMaskBlob::GetVersion() const
	{
		return m_header.m_version;
	}

	Bool RenderMultilayerMaskBlob::IsVersionCurrent() const
	{
		return m_header.m_version == CURRENT_VERSION;
	}

	Bool RenderMultilayerMaskBlob::IsVersionValid() const
	{
		return m_header.m_version >= MIN_VERSION && m_header.m_version <= CURRENT_VERSION;
	}

	Uint32 RenderMultilayerMaskBlob::GetDataSize() const
	{
		return m_atlasData.GetSize() + m_tilesData.GetSize();
	}


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	RenderMultilayerMaskResource::RenderMultilayerMaskResource() = default;
	RenderMultilayerMaskResource::~RenderMultilayerMaskResource() = default;


	void RenderMultilayerMaskResource::InitRenderResourceBlob( ECookingPlatform platform, const THandle< RenderMultilayerMaskBlob >& blob )
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

	const THandle< IRenderResourceBlob >& RenderMultilayerMaskResource::GetCookingPlatfromResourceBlob( ECookingPlatform platform ) const
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
		}

#endif
		static THandle< IRenderResourceBlob > nullResourceBlob;
		return nullResourceBlob;
	}

	void RenderMultilayerMaskResource::Clear()
	{
#ifndef NO_TEXTURE_EDITING
		m_renderResourceBlobPC.Reset();
		m_renderResourceBlobPS4.Reset();
		m_renderResourceBlobXboxOne.Reset();
#endif
	}

	const THandle< IRenderResourceBlob >& RenderMultilayerMaskResource::GetRenderResourceBlob() const
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

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderMultilayerMaskBlobPC, rend );
	RTTI_PARENT_TYPE( rend::RenderMultilayerMaskBlob );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderMultilayerMaskBlobXboxOne, rend );
	RTTI_PARENT_TYPE( rend::RenderMultilayerMaskBlob );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderMultilayerMaskBlobPS4, rend );
	RTTI_PARENT_TYPE( rend::RenderMultilayerMaskBlob );
RTTI_END_TYPE();
