/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "gizmoFwd.h"
#include "gizmoTypes.h"

namespace world
{
	enum class NodeScalingMode : Uint8;
}

namespace tools
{
	class SnapManager;

	/// A group of gizmos to do the scaling in arbitrary space
	class GizmoGroupScale
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:

		// Enum for axis selection, used for uniform scaling mode so we can enable/disable scaling on specific X/Y/Z axes. UniformXY scaling mode uses this.
		enum AxesMask : Uint8
		{
			AxesMaskX = RED_FLAG( 0 ),
			AxesMaskY = RED_FLAG( 1 ),
			AxesMaskZ = RED_FLAG( 2 ),
			AxesMaskXY = AxesMaskX | AxesMaskY,
			AxesMaskAll = AxesMaskX | AxesMaskY | AxesMaskZ
		};

		GizmoGroupScale( GizmoManager* manager, const GizmoTransformHandlerPtr& transformHandler, world::NodeScalingMode scalingMode );
		virtual ~GizmoGroupScale(); // automatically unregisters the gizmos

	private:
		GizmoManager* m_manager;
		red::DynArray< red::UniquePtr< IGizmo > > m_gizmos; // scaling boxes + the general wireframe box
	};

} // tools