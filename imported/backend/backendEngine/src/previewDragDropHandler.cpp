/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewDragDropHandler.h"

namespace tools
{
	IPreviewDragDropHandler::IPreviewDragDropHandler()
	{
	}

	IPreviewDragDropHandler::~IPreviewDragDropHandler()
	{
	}

	void IPreviewDragDropHandler::OnCanceled()
	{
		// handled in child classes
	}

	void IPreviewDragDropHandler::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{
		// handled in child classes
	}

	void IPreviewDragDropHandler::OnUpdate( float timeDelta )
	{
		// handled in child classes
	}

	void IPreviewDragDropHandler::OnMouseMoved( const InputMouseMove& evt )
	{
		// handled in child classes
	}

	Bool IPreviewDragDropHandler::OnFinished( const InputMouseButton& evt )
	{
		// handled in child classes
		return false;
	}

} // tools



