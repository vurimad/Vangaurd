/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "backendMemory.h"

class CRenderFrameInfo;

namespace tools
{
	struct InputKeyEvent;
	struct InputMouseClick;
	struct InputMouseButton;
	struct InputMouseMove;
	struct InputMouseWheel;

	class PreviewPanel;
	class IPreviewInputHandler;
	class IPreviewDragDropHandler;

	/// Capture less input listener interface
	/// The input events are "flowing" through a stack of the listener objects
	/// Listeners have the power to spawn the input handlers.
	/// Spawning the input handler will almost always capture the mouse.
	/// The keyboard events and mouse events are NOT routed through the listeners if there's an active handler
	/// with the exception of the OnModifierKeyUp/OnModifierKeyDown which should be used ONLY to detect function key combinations (Shift, Ctrl, Alt, etc)
	class BACKEND_ENGINE_API IPreviewInputListener
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		IPreviewInputListener();
		virtual ~IPreviewInputListener();

		virtual CName GetName() const;

		/// Render stuff related to this input listener
		/// Called after rendering of the scene and before rendering of the active input handler
		/// Good place to render "active viewport areas", widgets, and so on
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const;

		/// Notification about input handler being removed
		/// Needed for listeners and handlers that work closely together
		virtual void OnInputHandlerRemoved( PreviewPanel& panel, IPreviewInputHandler* handler, Bool wasCancelled );

		/// Update internal stuff related to this input listener
		/// Called from within the tick of the edit mode
		virtual void OnUpdate( float timeDelta );

		/// Notification about key being pressed
		/// Everybody receives this event and it cannot be eaten
		virtual void OnModifierKeyDown( EInputKey key );

		/// Notification about key being released
		/// Everybody receives this event and it cannot be eaten
		virtual void OnModifierKeyUp( EInputKey key );

		/// Notification that will always happen, (ignores consumption), includes all keys including modifiers and mouse buttons
		/// Called in addition to event types below, if applicable.
		/// Do not use this function to trigger events based on user input
		virtual void OnKeyEvent( EInputKey key, Bool keyIsDown );

		/// Handle key press, should return true if the key press was processed and should not be passed to other listeners
		/// Not called if there's an active input handler (all input is routed to it unconditionally)
		/// NOTE: mouse clicks are not passed though this
		virtual Bool OnKeyDown( PreviewPanel& panel, const InputKeyEvent& evt );

		/// Handle key released, should return true if the key press was processed and should not be passed to other listeners
		/// Not called if there's an active input handler (all input is routed to it unconditionally)
		/// NOTE: mouse clicks are not passed though this
		virtual Bool OnKeyUp( PreviewPanel& panel, const InputKeyEvent& evt );

		/// Handle mouse click, should return true if the key press was processed and should not be passed to other listeners
		/// Not called if there's an active input handler (all input is routed to it unconditionally)
		virtual Bool OnMouseDown( PreviewPanel& panel, const InputMouseButton& evt );

		/// Handle release of mouse button, should return true if the key press was processed and should not be passed to other listeners
		/// Not called if there's an active input handler (all input is routed to it unconditionally)
		virtual Bool OnMouseUp( PreviewPanel& panel, const InputMouseButton& evt );

		/// Handle click of mouse button, should return true if the key press was processed and should not be passed to other listeners
		/// Not called if there's an active input handler (all input is routed to it unconditionally)
		virtual Bool OnMouseClick( PreviewPanel& panel, const InputMouseClick& evt );

		/// Handle double click of mouse button, should return true if the key press was processed and should not be passed to other listeners
		/// Not called if there's an active input handler (all input is routed to it unconditionally)
		virtual Bool OnMouseDoubleClick( PreviewPanel& panel, const InputMouseClick& evt );

		/// Mouse is being moved, should return true if the key press was processed and should not be passed to other listeners
		/// Not called if there's an active input handler (all input is routed to it unconditionally)
		virtual Bool OnMouseMoved( PreviewPanel& panel, const InputMouseMove& evt );

		/// Handle mouse wheel, should return true if the key press was processed and should not be passed to other listeners
		/// Not called if there's an active input handler (all input is routed to it unconditionally)
		virtual Bool OnMouseWheel( PreviewPanel& panel,  const InputMouseWheel& evt );

		/// Handle Drag&Drop that wants to start
		/// Return created IPreviewDragDropHandler that will be used to service the event
		virtual IPreviewDragDropHandler* OnDragEnter( PreviewPanel& panel, const String& data, const InputMouseMove& evt );

		/// Handle drag leave notification
		virtual void OnDragLeave();

		/// Handle drag end notification
		virtual void OnDragDrop();
	};

} // tools
