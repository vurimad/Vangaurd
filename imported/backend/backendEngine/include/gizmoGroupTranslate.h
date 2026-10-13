/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "gizmoTypes.h"

namespace tools
{
	class IGizmo;

	/// A group of gizmos (arrows) to do the translation in arbitrary space
	class GizmoGroupTranslate
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		GizmoGroupTranslate( GizmoManager* manager, const GizmoTransformHandlerPtr& transformHandler, const Uint8 axisMask = 7, const Bool hasPlanes = true );
		virtual ~GizmoGroupTranslate(); // automatically unregisters the gizmos

	private:
		GizmoManager* m_manager;
		red::DynArray< red::UniquePtr< IGizmo > > m_gizmos;
	};

} // tools