#include "build.h"
#include "mesh.h"
#include "meshParamGpuBuffer.h"
#include "meshDataViewRO.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamGpuBuffer, mesh);
	RTTI_PARENT_TYPE(mesh::MeshParameter);
	RTTI_PROPERTY( m_stride );
	RTTI_PROPERTY( m_data );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamTopologyData, mesh);
	RTTI_PARENT_TYPE(mesh::MeshParameter);
	RTTI_PROPERTY( m_data );
	RTTI_PROPERTY( m_offsets );
	RTTI_PROPERTY( m_sizes );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamTopologyMetadata, mesh);
	RTTI_PARENT_TYPE(mesh::MeshParameter);
	RTTI_PROPERTY( m_data );
	RTTI_PROPERTY( m_offsets );
	RTTI_PROPERTY( m_sizes );
RTTI_END_TYPE();

namespace mesh
{
	MeshParamGpuBuffer::MeshParamGpuBuffer( Uint32 stride, Uint32 dataSize, const void* dataBuffer )
		: m_stride( stride )
		, m_data( DataBuffer::Copy( dataBuffer, dataSize ) )
	{
	}

	void MeshParamGpuBuffer::OnMeshChanged( const CMesh& mesh )
	{
	}

	//////////////////////////////////////////////////////////////////////////

	MeshParamTopologyData::MeshParamTopologyData( const DataBuffer& dataBuffer, const red::DynArray< Uint32 >& offsets, const red::DynArray< Uint32 >& sizes )
		: m_data( DataBuffer::Copy( dataBuffer ) )
		, m_offsets( offsets )
		, m_sizes( sizes )
	{
		RED_ASSERT( offsets.Size() == sizes.Size() );
	}

	void MeshParamTopologyData::OnMeshChanged( const CMesh& mesh )
	{
	}

	void MeshParamTopologyData::OnPostLoad(const PostLoadContext& context)
	{
		if ( IsGameMode() )
		{
			m_data.Clear();
			m_sizes.Clear();
			m_offsets.Clear();
		}

#ifndef RED_CONFIGURATION_FINAL
		RED_ASSERT( m_offsets.Size() == m_sizes.Size() );
		if ( !m_data.Empty() && GetParent() )
		{
			if ( const CMesh* mesh = SafeCast< const CMesh >( GetParent() ) )
			{
				Uint32 chunksCount = mesh::DataViewRO( *mesh ).GetNumChunks();
				if ( m_offsets.Size() < chunksCount )
				{
					RED_DATA_ERROR_BEGIN( red::LoggerCategory_Characters, false, "Mesh is missing topology data! Runtime tangent update will not work, please reimport mesh!\r\nMesh file '%hs'\r\n", mesh->GetPath().ToDebugString() );
					RED_DATA_ERROR_ACTION_SHOW_ASSET( mesh->GetPath().ToDebugString() );
					RED_DATA_ERROR_END();

					const Uint32 diff = chunksCount - m_offsets.Size();
					m_offsets.Reserve( chunksCount );
					for ( Uint32 i = 0; i < diff; ++i )
					{
						m_offsets.PushBack( 0 );
						m_sizes.PushBack( 0 );
					}
				}
			}
		}
#endif // RED_CONFIGURATION_FINAL
	}

	//////////////////////////////////////////////////////////////////////////

	MeshParamTopologyMetadata::MeshParamTopologyMetadata( const DataBuffer& dataBuffer, const red::DynArray< Uint32 >& offsets, const red::DynArray< Uint32 >& sizes )
		: m_data( DataBuffer::Copy( dataBuffer ) )
		, m_offsets( offsets )
		, m_sizes( sizes )
	{
		RED_ASSERT( offsets.Size() == sizes.Size() );
	}

	void MeshParamTopologyMetadata::OnMeshChanged( const CMesh& mesh )
	{
	}

	void MeshParamTopologyMetadata::OnPostLoad(const PostLoadContext& context)
	{
		if ( IsGameMode() )
		{
			m_data.Clear();
			m_sizes.Clear();
			m_offsets.Clear();
		}

#ifndef RED_CONFIGURATION_FINAL
		RED_ASSERT( m_offsets.Size() == m_sizes.Size() );
		if ( !m_data.Empty() && GetParent() )
		{
			if ( const CMesh* mesh = SafeCast< const CMesh >( GetParent() ) )
			{
				const Uint32 chunksCount = mesh::DataViewRO( *mesh ).GetNumChunks();
				if (m_offsets.Size() < chunksCount)
				{
					RED_DATA_ERROR_BEGIN( red::LoggerCategory_Characters, false, "Mesh is missing topology data! Runtime tangent update will not work, please reimport mesh!\r\nMesh file '%hs'\r\n", mesh->GetPath().ToDebugString() );
					RED_DATA_ERROR_ACTION_SHOW_ASSET( mesh->GetPath().ToDebugString() );
					RED_DATA_ERROR_END();

					const Uint32 diff = chunksCount - m_offsets.Size();
					m_offsets.Reserve( chunksCount );
					for ( Uint32 i = 0; i < diff; ++i )
					{
						m_offsets.PushBack( 0 );
						m_sizes.PushBack( 0 );
					}
				}
			}
		}
#endif // RED_CONFIGURATION_FINAL
	}

} // mesh