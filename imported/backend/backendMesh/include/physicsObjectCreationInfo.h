/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace tools
{
	struct SPhysicsBaseCreationInfo
	{
		RTTI_DECLARE_TYPE( SPhysicsBaseCreationInfo )

		Uint32 m_type;
		Uint32 m_data;
		Uint32 m_vertexLimit = 64; // for convexes
		Bool m_isObstacle = false;	 					
		red::DynArray<Vector3> m_selectedVertices;
	};
}
