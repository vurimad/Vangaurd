/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redReflection/include/resource.h"

namespace mesh
{
	struct BACKEND_MESH_API MeshCookingArtifact final : public CResource
	{
		IMPLEMENT_RESOURCE_INTERFACE( "", "mesh_artifact", "Mesh cooking artifact that stores data from different worlds and is used to generate the depot cooking artifact" );
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( MeshCookingArtifact );
		RED_BASE_CLASS( CResource );

		void AppendStaticMesh( const res::ResourcePath meshPath );
		void AppendDynamicMesh( const res::ResourcePath meshPath );
		void OnPreSave();

		red::DynArray< Uint64 > m_staticMeshes{ red::PoolBackend() };
		red::DynArray< Uint64 > m_dynamicMeshes{ red::PoolBackend() };
		red::Set< res::ResourcePath > m_staticMeshPaths{ red::PoolBackend() };
		red::Set< res::ResourcePath > m_dynamicMeshPaths{ red::PoolBackend() };

	};
}