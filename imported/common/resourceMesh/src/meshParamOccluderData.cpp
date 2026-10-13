/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshParamOccluderData.h"
#include "../../renderData/include/visibilityOccluderResource.h"
#include "../../renderData/include/visibilityWorldCommon.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshParamOccluderData, mesh );
	RTTI_PARENT_TYPE( mesh::MeshParameter );
	RTTI_PROPERTY( m_occluderResource );
	RTTI_PROPERTY( m_defaultOccluderType ).editable();
	RTTI_PROPERTY( m_autoHideDistanceScale ).range( 0, 255 ).editable();
RTTI_END_TYPE();


namespace mesh
{

	MeshParamOccluderData::MeshParamOccluderData()
		: m_defaultOccluderType(vis::WorldOccluderType::None)
		, m_autoHideDistanceScale(UINT8_MAX)
	{
	}

	void MeshParamOccluderData::OnMeshChanged( const CMesh& mesh )
	{
	}

	void MeshParamOccluderData::SetOccluderResource( const THandle<vis::IOccluderResource>& newResource )
	{
		if ( m_occluderResource != newResource )
		{
			m_occluderResource = newResource;
		}
	}

	vis::WorldOccluderType MeshParamOccluderData::GetDefaultOccluderType() const
	{
		if ( m_defaultOccluderType == vis::WorldOccluderType::Default )
		{
			return vis::WorldOccluderType::None;
		}
		else
		{
			return m_defaultOccluderType;
		}
	}

	void MeshParamOccluderData::SetDefaultOccluderType( vis::WorldOccluderType defaultType )
	{
		// #TODO: Maybe disallow setting WorldOccluderType::Default?
		m_defaultOccluderType = defaultType;
	}

} // mesh
