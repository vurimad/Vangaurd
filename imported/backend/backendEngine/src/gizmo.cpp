/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gizmo.h"

namespace tools
{
	IGizmo::IGizmo()
	{
	}

	IGizmo::~IGizmo()
	{
	}

	Bool IGizmo::OnCanRender( const CRenderCamera& camera )
	{
		return true;
	}

	Bool IGizmo::OnCheckCollision( Int32 x, Int32 y, const EditModeViewport& vpt, Priority& priority )
	{
		return false;
	}

	void IGizmo::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd, ERenderMode mode )
	{
	}

	IGizmo::ECursor IGizmo::OnGetCursor()
	{
		return ECursor::Normal;
	}

	Float IGizmo::CalcDistanceToSegment( const Vector2& p, const Vector2& a, const Vector2& b )
	{
		Vector2 dir = b - a; 
		const Float len = dir.Mag();
		dir.Normalize();
		const Float dot = dir.Dot( p-a );

		if ( dot <= 0.0f )
		{
			return (p-a).Mag();
		}
		else if ( dot >= len )
		{
			return (p-b).Mag();
		}
		else
		{
			Vector2 r = a + dir * dot;
			return (p-r).Mag();
		} 
	}

} // tools

