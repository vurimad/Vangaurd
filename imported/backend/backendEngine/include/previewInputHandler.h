/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "backendMemory.h"

class CRenderFrameInfo;

namespace tools
{
	struct InputKeyEvent;
	struct InputMouseButton;
	struct InputMouseClick;
	struct InputMouseMove;
	struct InputMouseWheel;

	// FIXME: Due to gizmo was implemented base on IPreviewInputHandler, this blocks possibility to
	// implement preview operations that use gizmos inside.

	// PlEASE USE IPreviewCommand instead

	/// An transient input handler for various user operations in the viewport
	/// This is the only legal way to implement user interaction with the viewport and it's content
	/// Input handlers are instantiated on top of the active edit mode
	/// Input handler is created in reaction to some initial user input
	/// ALL INPUT IS ROUTED TO AND EATED BY THE INPUT HANDLER if it exists
	/// Input handler reacts to user input and besides doing it's things it can mutate to other input handler
	/// For now all input handlers always capture the mouse input
	/// For now all input handlers can always be canceled by pressing ESC key
	/// The input handler can be externally canceled when the edit mode changes or the viewport lost focus

	class BACKEND_ENGINE_API IPreviewInputHandler
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_POLYMORPHIC_TYPE( IPreviewInputHandler );

	public:
		IPreviewInputHandler();
		virtual ~IPreviewInputHandler();

		/// Canceled - called when edit mode is changed, viewport loses focus or there's a new input handler coming
		/// NOTE: If the input handler is owning the mouse capture it's automatically released
		virtual void OnCanceled();

		/// Render stuff related to this debug mode
		/// Called when input handler is active to render it's whatever shit on top of the viewport
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const;

		/// Handle key pressed, should return true if the key was handled
		/// NOTE: mouse clicks are not passed though this
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual Bool OnKeyDown( const InputKeyEvent& evt );

		/// Handle key released, called only if the matching key press event was also handled. Should return true if the key was handled.
		/// NOTE: mouse clicks are not passed though this
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual Bool OnKeyUp( const InputKeyEvent& evt );

		/// Handle mouse click, should return true if the key was handled
		/// Passed the current rendering camera as a reference for any kind of raytracing
		/// Modify the captureMouse parameter to indicate if the mouse should be captured or not
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual Bool OnMouseDown( const InputMouseButton& evt );

		/// Handle release of mouse button, should return true if it was handled
		/// Modify the captureMouse parameter to indicate if the mouse should be captured or not
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual Bool OnMouseUp( const InputMouseButton& evt );

		/// Handle click of mouse button, should return true if it was handled
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual Bool OnMouseClick( const InputMouseClick& evt );

		/// Handle double click of mouse button, should return true if it was handled
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual Bool OnMouseDoubleClick( const InputMouseClick& evt );

		/// Mouse is being moved
		/// NOTE: the button mask represents the combination of pressed mouse buttons
		/// NOTE: the capture flag represents the mouse capture state and cannot be changed
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual Bool OnMouseMoved( const InputMouseMove& evt );

		/// Handle mouse wheel
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual Bool OnMouseWheel( const InputMouseWheel& evt );

		/// Update internal stuff related to this debug mode
		/// Called from within the tick of the edit mode
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual Bool OnUpdate( float timeDelta );
	};

	// It can set IPreviewInputHandlers for command needs
	
	class BACKEND_ENGINE_API IPreviewCommand
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_POLYMORPHIC_TYPE( IPreviewCommand );

	public:
		IPreviewCommand();
		virtual ~IPreviewCommand();

		/// Render stuff related to this debug mode
		/// Called when input handler is active to render it's whatever shit on top of the viewport
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const;

		/// Canceled - called when edit mode is changed, viewport loses focus or there's a new input handler coming
		/// NOTE: If the input handler is owning the mouse capture it's automatically released
		virtual void OnCanceled();
		virtual void OnFinished();

		struct InputResult
		{
			void Handle( Bool handled )
			{
				m_keepLive = handled;
				m_handled = handled;
			}

			Bool m_handled{ false };
			Bool m_keepLive{ true };

			// when not m_handled but m_keepLive is set to true, indicate if we check specific listeners

			Bool m_processGizmoManager{ true };
			Bool m_processCamera{ true };
		};

		/// Handle key pressed, should return true if the key was handled
		/// NOTE: mouse clicks are not passed though this
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual void OnKeyDown( const InputKeyEvent& evt, InputResult& result );

		/// Handle key released, called only if the matching key press event was also handled. Should return true if the key was handled.
		/// NOTE: mouse clicks are not passed though this
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual void OnKeyUp( const InputKeyEvent& evt, InputResult& result );

		/// Handle mouse click, should return true if the key was handled
		/// Passed the current rendering camera as a reference for any kind of raytracing
		/// Modify the captureMouse parameter to indicate if the mouse should be captured or not
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual void OnMouseDown( const InputMouseButton& evt, InputResult& result );

		/// Handle release of mouse button, should return true if it was handled
		/// Modify the captureMouse parameter to indicate if the mouse should be captured or not
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual void OnMouseUp( const InputMouseButton& evt, InputResult& result );

		/// Handle click of mouse button, should return true if it was handled
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual void OnMouseClick( const InputMouseClick& evt, InputResult& result );

		/// Handle double click of mouse button, should return true if it was handled
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual void OnMouseDoubleClick( const InputMouseClick& evt, InputResult& result );

		/// Mouse is being moved
		/// NOTE: the button mask represents the combination of pressed mouse buttons
		/// NOTE: the capture flag represents the mouse capture state and cannot be changed
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual void OnMouseMoved( const InputMouseMove& evt, InputResult& result );

		/// Handle mouse wheel
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual void OnMouseWheel( const InputMouseWheel& evt, InputResult& result );

		/// Update internal stuff related to this debug mode
		/// Called from within the tick of the edit mode
		/// Return 'true' to keep the existing input handler alive or 'false' to abort
		virtual Bool OnUpdate( float timeDelta );
	};
} // tools
