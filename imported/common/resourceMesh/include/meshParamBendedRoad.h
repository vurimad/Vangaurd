/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "meshParameter.h"

namespace mesh
{

	class RESOURCEMESH_API MeshParamBendedRoad : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamBendedRoad );

	public:
		MeshParamBendedRoad();
		virtual void OnMeshChanged( const CMesh& mesh ) override final;

		red::DynArray< Uint16 > m_occInds;
		red::DynArray< Vector4 > m_occVerts;
		red::DynArray< Vector4> m_occSkinWeights;
		red::DynArray< Color > m_occSkinInds;

		red::DynArray < red::DynArray< Uint16>> m_collInds;
		red::DynArray < red::DynArray< Vector3>> m_collVerts;
		red::DynArray < red::DynArray< Uint16>> m_collFaceMatInds;
		red::DynArray < red::DynArray< String>> m_collFaceMaterialNames;
		red::DynArray < red::DynArray< Vector4>> m_collSkinWeights;
		red::DynArray < red::DynArray< Color>> m_collSkinInds;
		red::DynArray < String> m_collMaterialName;
		red::DynArray < String> m_collFilterPresetName;
		
	};

} // mesh
