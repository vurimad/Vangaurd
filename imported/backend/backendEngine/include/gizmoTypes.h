/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "previewFwd.h"


namespace tools
{
	
	enum class GizmoType : Uint8
	{
		Translation = 0,
		Rotation = 1,
		Scale = 2,

		Count = 3
	};

	enum class GizmoSpaceType : Uint8
	{
		World,
		Local,
		// TODO Parent

		Count,
	};

	enum class GizmoTransformMode : Uint8
	{
		Objects,				// transform selected object relatively to the gizmo pivot (affects objects)
		ObjectsIndividually,	// transform selected objects relatively to their pivots (affects objects)
		ObjectsPivots,			// transform the pivots of selected objects (affects objects)
	};

} // tools