/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "meshParameter.h"
#include "../../../common/renderData/include/speedTreeWind.h"

class CMesh;

namespace mesh
{
	struct RESOURCEMESH_API MeshParamSpeedTreeWind : public mesh::MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamSpeedTreeWind );
		
		MeshParamSpeedTreeWind();
		~MeshParamSpeedTreeWind();

		virtual void OnMeshChanged(const CMesh& mesh) override final;

		// Serialization
		virtual void OnSerialize(IFile& file) override;

		CSpeedTreeWind m_speedTreeWind;	
	};
}
