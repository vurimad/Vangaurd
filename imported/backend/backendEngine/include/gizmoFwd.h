/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace tools
{

	class IGizmo;
	class IGizmoLines;
	class GizmoCollection;
	class GizmoGroupTranslate;
	class GizmoGroupRotate;
	class GizmoGroupScale;
	class GizmoManager;
	class GizmoDirectManager;
	class GizmoTransformHandler;
	typedef red::SharedPtr< GizmoTransformHandler > GizmoTransformHandlerPtr;
	typedef red::WeakPtr< GizmoTransformHandler > GizmoTransformHandlerWeakPtr;

} // tools