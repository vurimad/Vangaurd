/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "backendMemory.h"

namespace tools
{
	struct InputMouseButton;
	struct InputMouseClick;
	struct InputMouseMove;

	/// An transient input handler for handling Drag&Drop events coming from the C# overlay
	/// This is the only legal way to implement Drag&Drop in the viewport
	/// The drag&drop handler can be externally canceled when the edit mode changes or the viewport lost focus
	class BACKEND_ENGINE_API IPreviewDragDropHandler
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		IPreviewDragDropHandler();
		virtual ~IPreviewDragDropHandler();

		/// Canceled - called when edit mode is changed, viewport loses focus or there's a new input handler coming
		/// NOTE: If the input handler is owning the mouse capture it's automatically released
		virtual void OnCanceled();

		/// Render stuff related to this debug mode
		/// Called when input handler is active to render it's whatever shit on top of the viewport
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const;

		/// Update internal stuff related to this debug mode
		/// Called from within the tick of the edit mode
		virtual void OnUpdate( float timeDelta );

		/// Target position moved
		virtual void OnMouseMoved( const InputMouseMove& evt );

		/// Drop finished
		virtual Bool OnFinished( const InputMouseButton& evt );
	};

} // tools
