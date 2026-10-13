#pragma once
/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "meshParameter.h"


namespace mesh
{
	class RESOURCEMESH_API MeshParamWorkspotOffsets : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamWorkspotOffsets );

	public:
		MeshParamWorkspotOffsets(){};

		const Matrix& GetOffset( const CName& ) const;
		RED_INLINE const red::DynArray< CName >& GetOffsetsNames() const { return m_names; };
		void SetWorkspotOffsets( const red::DynArray< CName >&, const red::DynArray< Matrix >& );

	private:
		virtual void OnMeshChanged( const CMesh& mesh ) override;

		red::DynArray< CName > m_names{ rend::PoolMesh() };
		red::DynArray< Matrix > m_offsets{ rend::PoolMesh() };

		static const Matrix m_unknownOffset;
	};
}
