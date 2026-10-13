/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "meshParameter.h"

namespace mesh
{

	class RESOURCEMESH_API MeshParamGarmentSupport : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamGarmentSupport );

	public:
		MeshParamGarmentSupport();
		//MeshParamGarmentSupport( const red::DynArray< red::DynArray <Uint32>>& chunkCapVerts, bool customMorph);

		virtual void OnMeshChanged( const CMesh& mesh ) override final;

		const red::DynArray < red::DynArray<Uint32>>& GetChunksCapVerts() const { return m_chunkCapVertices; };
		const bool HasCustomMorphs() const { return m_customMorph; };

		bool m_customMorph;
		red::DynArray < red::DynArray<Uint32>> m_chunkCapVertices;
		
	};

} // mesh
