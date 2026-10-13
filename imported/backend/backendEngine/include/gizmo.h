/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "previewFwd.h"
#include "gizmoTypes.h"

class CRenderFrame;

namespace tools
{
	
	/// base gizmo implementation
	class IGizmo
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		IGizmo();
		virtual ~IGizmo();

		enum class ERenderMode : Uint8
		{
			Normal,			// normal rendering, no other gizmo is active
			Selected,		// gizmo is selected but no gizmo is active
			Active,			// this gizmo is active
			OtherActive,	// other gizmo is active
		};

		enum class ECursor : Uint8
		{
			Normal,
			Hand,
			ResizeNS,
			ResizeWE,
			ResizeNESW,
			ResizeNWSE,
			Denied,
		};

		enum Priority : Uint8
		{
			Priority_Low = 0,
			Priority_Mid = 1,
			Priority_High = 2,
		};

		/// Dynamic filter, allows to skip widgets that are aligned in a not very nice way with camera
		virtual Bool OnCanRender( const CRenderCamera& camera );

		/// Check collision with widget, must be implemented
		/// Additionally, returns collided gizmo priority
		virtual Bool OnCheckCollision( Int32 x, Int32 y, const EditModeViewport& vpt, Priority& outPriority ) = 0;

		/// Generate rendering fragments for this gizmo
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd, ERenderMode mode ) = 0;

		/// Activate this widget - generate input handler
		virtual red::UniquePtr< IPreviewInputHandler > OnActivate( const GizmoTransformMode transformMode ) = 0;

		/// Get mouse cursor that should be displayed if this widget is active
		virtual ECursor OnGetCursor();

		virtual void GetDepth( const EditModeViewport& vpt, Float& outMin, Float& outMax ) const = 0;

		virtual Bool SupportsCloning() const { return false; }
		virtual Bool SupportsPivotMovement() const { return false; }

	public:
		// Utility, compute distance (in 2D) between point and a segment
		static Float CalcDistanceToSegment( const Vector2& p, const Vector2& a, const Vector2& b );
	};

} // tools