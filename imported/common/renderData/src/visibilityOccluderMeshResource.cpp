/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "visibilityOccluderMeshResource.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( OccluderMeshResource, vis );
	RTTI_PARENT_TYPE( vis::IOccluderResource );
	RTTI_PROPERTY( m_resourceVersion );
	RTTI_PROPERTY( m_vertices );
	RTTI_PROPERTY( m_indices );
	RTTI_PROPERTY( m_boundingBox );
	RTTI_PROPERTY( m_twoSided );
RTTI_END_TYPE();


namespace vis
{

	struct OccluderMeshResourceVersions
	{
		enum : Uint32
		{
			VER0_Invalid,
			VER1_InitialImplementation,
			VER2_AddResourceHash,
		};
	};
	const Uint32 OccluderMeshResource::LATEST_RESOURCE_VERSION = OccluderMeshResourceVersions::VER2_AddResourceHash;


	OccluderMeshResource::OccluderMeshResource()
		: m_resourceVersion(OccluderMeshResourceVersions::VER0_Invalid)
		, m_vertices{ rend::PoolOccluderShapes() }
		, m_indices{ rend::PoolOccluderShapes() }
	{
	}

	red::ArraySpan<const math::Vector4> OccluderMeshResource::GetVertices() const
	{
		const math::Vector4* verticesPtr = static_cast<const math::Vector4*>(m_vertices.Data());
		const Uint32 verticesCount = m_vertices.Size() / sizeof(math::Vector4);

		return red::ArraySpan<const math::Vector4>(verticesPtr, verticesCount);
	}

	red::ArraySpan<const Uint16> OccluderMeshResource::GetIndices() const
	{
		const Uint16* indicesPtr = static_cast<const Uint16*>(m_indices.Data());
		const Uint32 indicesCount = m_indices.Size() / sizeof(Uint16);
		return red::ArraySpan<const Uint16>(indicesPtr, indicesCount);
	}



	void OccluderMeshResource::OnPostLoad( const PostLoadContext& context )
	{
		TBaseClass::OnPostLoad( context );

		if ( m_resourceVersion < OccluderMeshResourceVersions::VER2_AddResourceHash )
		{
			m_resourceHash = CalculateResourceHash();
			m_resourceVersion = OccluderMeshResourceVersions::VER2_AddResourceHash;
		}

		RED_ASSERT(m_resourceVersion == LATEST_RESOURCE_VERSION, "OccluderMeshResource is not at latest version! In-place update failed.");
	}

	Uint32 OccluderMeshResource::CalculateResourceHash() const
	{
		Uint32 resourceHash = red::CalculateHash32( m_vertices.Data(), m_vertices.Size() );
		resourceHash = red::CalculateHash32( m_indices.Data(), m_indices.Size(), resourceHash );
		resourceHash = red::CalculateHash32( &m_twoSided, sizeof(Bool), resourceHash );
		return resourceHash;
	}


	THandle<IOccluderResource> OccluderMeshResource::Create( const CreateInfo& createInfo )
	{
		THandle<OccluderMeshResource> newResource( RED_NEW(OccluderMeshResource)() );

		newResource->m_resourceVersion = LATEST_RESOURCE_VERSION;

		newResource->m_vertices = DataBuffer::Copy(createInfo.vertices.Data(), createInfo.vertices.SizeInBytes(), rend::PoolOccluderShapes());
		newResource->m_indices = DataBuffer::Copy(createInfo.indices.Data(), createInfo.indices.SizeInBytes(), rend::PoolOccluderShapes());

		newResource->m_boundingBox = createInfo.boundingBox;
		newResource->m_twoSided = createInfo.twoSided;

		newResource->m_resourceHash = newResource->CalculateResourceHash();

		return newResource;
	}
}
