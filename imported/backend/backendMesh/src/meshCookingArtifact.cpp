#include "build.h"
#include "meshCookingArtifact.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshCookingArtifact, mesh )
	RTTI_PARENT_TYPE( CResource );
RTTI_PROPERTY( m_staticMeshes );
RTTI_PROPERTY( m_dynamicMeshes );
RTTI_END_TYPE();

namespace mesh
{
	void MeshCookingArtifact::AppendStaticMesh( const res::ResourcePath meshPath )
	{
		if( !meshPath.IsValid() )
		{
			return;
		}

		if( !m_dynamicMeshPaths.Exist( meshPath ) )
		{
			m_staticMeshPaths.Insert( meshPath );
		}
	}

	void MeshCookingArtifact::AppendDynamicMesh( const res::ResourcePath meshPath )
	{
		if( !meshPath.IsValid() )
		{
			return;
		}

		m_dynamicMeshPaths.Insert( meshPath );
		m_staticMeshPaths.Remove( meshPath );
	}

	void MeshCookingArtifact::OnPreSave()
	{
		m_staticMeshes.Reserve( m_staticMeshPaths.Size() );
		m_dynamicMeshes.Reserve( m_dynamicMeshPaths.Size() );

		for( auto& path : m_staticMeshPaths )
		{
			m_staticMeshes.PushBack( path.GetHash() );
		}

		for( auto& path : m_dynamicMeshPaths )
		{
			m_dynamicMeshes.PushBack( path.GetHash() );
		}
	}
}
