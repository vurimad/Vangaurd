/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "meshParameter.h"


namespace vis 
{ 
	class IOccluderResource; 
	enum class WorldOccluderType : Uint8;
}

namespace mesh
{
	class RESOURCEMESH_API MeshParamOccluderData : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamOccluderData );

	public:
		MeshParamOccluderData();

		const THandle<vis::IOccluderResource>& GetOccluderResource() const { return m_occluderResource; }
		void SetOccluderResource( const THandle<vis::IOccluderResource>& newResource );

		vis::WorldOccluderType GetDefaultOccluderType() const;
		void SetDefaultOccluderType( vis::WorldOccluderType defaultType );

		Float GetAutoHideDistanceScale() const { return (Float)m_autoHideDistanceScale / 255.0f; }

	private:
		virtual void OnMeshChanged(const CMesh& mesh) override;

		THandle<vis::IOccluderResource> m_occluderResource;
		vis::WorldOccluderType m_defaultOccluderType;
		Uint8 m_autoHideDistanceScale;
	};
}
