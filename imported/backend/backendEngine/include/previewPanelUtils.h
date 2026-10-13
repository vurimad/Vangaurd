/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace tools
{
	// Render grid at point (0,0,0)
	extern void RenderGrid( rend::IDebugDrawer& dd );

	/// Render flat 3D grid centered at given point
	extern void Render3DGrid( rend::IDebugDrawer& dd, const Vector3& center, const Color& gridColor = Color(120, 120, 120), const Color axisColor = Color(120, 180, 180) );

} // tools
