/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "gizmoFwd.h"
#include "gizmoTypes.h"

namespace tools
{
	class SnapManager;

	/// A group of gizmos to do the rotation in arbitrary space
	class GizmoGroupRotate
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		GizmoGroupRotate( GizmoManager* manager, const GizmoTransformHandlerPtr& transformHandler, const Uint8 axisMask = 7 );
		virtual ~GizmoGroupRotate(); // automatically unregisters the gizmos

	private:
		GizmoManager* m_manager;
		SnapManager* m_snapManager;

		red::DynArray< red::UniquePtr< IGizmo > > m_gizmos;
		
	};

} // tools
