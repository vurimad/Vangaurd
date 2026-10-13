/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewInput.h"
#include "previewInputHandler.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IPreviewInputHandler, tools );
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IPreviewCommand, tools );
RTTI_END_TYPE();

namespace tools
{
	IPreviewInputHandler::IPreviewInputHandler()
	{
	}

	IPreviewInputHandler::~IPreviewInputHandler()
	{
	}

	void IPreviewInputHandler::OnCanceled()
	{
	}

	void IPreviewInputHandler::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{
		// nothing
	}

	Bool IPreviewInputHandler::OnKeyDown( const InputKeyEvent& evt )
	{
		return true; // do not disable the input handler
	}

	Bool IPreviewInputHandler::OnKeyUp( const InputKeyEvent& evt )
	{
		return true; // do not disable the input handler
	}

	Bool IPreviewInputHandler::OnMouseDown( const InputMouseButton& evt )
	{
		return true; // do not disable the input handler
	}

	Bool IPreviewInputHandler::OnMouseUp( const InputMouseButton& evt )
	{
		return true; // do not disable the input handler
	}

	Bool IPreviewInputHandler::OnMouseClick( const InputMouseClick& evt )
	{
		return true; // do not disable the input handler
	}

	Bool IPreviewInputHandler::OnMouseDoubleClick( const InputMouseClick& evt )
	{
		return true; // do not disable the input handler
	}

	Bool IPreviewInputHandler::OnMouseMoved( const InputMouseMove& evt )
	{
		return true; // do not disable the input handler
	}

	Bool IPreviewInputHandler::OnMouseWheel( const InputMouseWheel& evt )
	{
		return true; // do not disable the input handler
	}

	Bool IPreviewInputHandler::OnUpdate( float timeDelta )
	{
		return true; // do not disable the input handler
	}

	///
	///
	///

	IPreviewCommand::IPreviewCommand()
	{
	}

	IPreviewCommand::~IPreviewCommand()
	{
	}

	void IPreviewCommand::OnFinished()
	{
	}

	void IPreviewCommand::OnCanceled()
	{
	}

	void IPreviewCommand::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{
		// nothing
	}

	void IPreviewCommand::OnKeyDown( const InputKeyEvent& evt, InputResult& result )
	{
		result.m_keepLive = true; // do not disable the input handler
	}

	void IPreviewCommand::OnKeyUp( const InputKeyEvent& evt, InputResult& result )
	{
		result.m_keepLive = true; // do not disable the input handler
	}

	void IPreviewCommand::OnMouseDown( const InputMouseButton& evt, InputResult& result )
	{
		result.m_keepLive = true; // do not disable the input handler
	}

	void IPreviewCommand::OnMouseUp( const InputMouseButton& evt, InputResult& result )
	{
		result.m_keepLive = true; // do not disable the input handler
	}

	void IPreviewCommand::OnMouseClick( const InputMouseClick& evt, InputResult& result )
	{
		result.m_keepLive = true; // do not disable the input handler
	}

	void IPreviewCommand::OnMouseDoubleClick( const InputMouseClick& evt, InputResult& result )
	{
		result.m_keepLive = true; // do not disable the input handler
	}

	void IPreviewCommand::OnMouseMoved( const InputMouseMove& evt, InputResult& result )
	{
		result.m_keepLive = true; // do not disable the input handler
	}

	void IPreviewCommand::OnMouseWheel( const InputMouseWheel& evt, InputResult& result )
	{
		result.m_keepLive = true; // do not disable the input handler
	}

	Bool IPreviewCommand::OnUpdate( float timeDelta )
	{
		return true; // do not disable the input handler
	}
} // tools