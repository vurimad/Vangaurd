/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewInput.h"
#include "previewInputListener.h"

namespace tools
{
	IPreviewInputListener::IPreviewInputListener()
	{
	}

	IPreviewInputListener::~IPreviewInputListener()
	{
	}

	CName IPreviewInputListener::GetName() const
	{
		return CName::NONE();
	}

	void IPreviewInputListener::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{
	}

	void IPreviewInputListener::OnInputHandlerRemoved( PreviewPanel&, IPreviewInputHandler*, Bool )
	{
	}

	void IPreviewInputListener::OnUpdate( float timeDelta )
	{
	}

	void IPreviewInputListener::OnModifierKeyDown( EInputKey )
	{
	}

	void IPreviewInputListener::OnModifierKeyUp( EInputKey )
	{
	}

	void IPreviewInputListener::OnKeyEvent( EInputKey, Bool )
	{
	}

	Bool IPreviewInputListener::OnKeyDown( PreviewPanel& panel, const InputKeyEvent& evt )
	{
		return false; // not handled
	}

	Bool IPreviewInputListener::OnKeyUp( PreviewPanel& panel, const InputKeyEvent& evt )
	{
		return false; // not handled
	}

	Bool IPreviewInputListener::OnMouseDown( PreviewPanel& panel, const InputMouseButton& evt )
	{
		return false; // not handled
	}

	Bool IPreviewInputListener::OnMouseUp( PreviewPanel& panel, const InputMouseButton& evt )
	{
		return false; // not handled
	}

	Bool IPreviewInputListener::OnMouseClick( PreviewPanel& panel, const InputMouseClick& evt )
	{
		return false; // not handled
	}

	Bool IPreviewInputListener::OnMouseDoubleClick( PreviewPanel& panel, const InputMouseClick& evt )
	{
		return false; // not handled
	}

	Bool IPreviewInputListener::OnMouseMoved( PreviewPanel& panel, const InputMouseMove& evt )
	{
		return false; // not handled
	}

	Bool IPreviewInputListener::OnMouseWheel( PreviewPanel& panel, const InputMouseWheel& evt )
	{
		return false; // not handled
	}

	IPreviewDragDropHandler* IPreviewInputListener::OnDragEnter( PreviewPanel& panel, const String& data, const InputMouseMove& evt )
	{
		return nullptr; // not handled by default
	}

	void IPreviewInputListener::OnDragLeave()
	{
	}

	void IPreviewInputListener::OnDragDrop()
	{
	}

} // tools

