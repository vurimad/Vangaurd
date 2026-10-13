/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "previewShortcutInputHandler.h"

namespace tools
{
	PreviewShortcutInputHandler::PreviewShortcutInputHandler( const Uint32 panelID )
		: m_panelID( panelID )
	{
	}

	PreviewShortcutInputHandler::~PreviewShortcutInputHandler() = default;

	RED_INLINE EInputKey PreviewShortcutInputHandler::MouseButtonToInputKey( EMouseButton button ) const
	{
		switch ( button )
		{
		case LMB:
			return EInputKey::IK_LeftMouse;

		case RMB:
			return EInputKey::IK_RightMouse;

		case MMB:
			return EInputKey::IK_MiddleMouse;
		}

		RED_FATAL( "Unrecognised mouse button: %i", button );
		return EInputKey::IK_None;
	}

	RED_INLINE Bool PreviewShortcutInputHandler::InputEventInternal( EInputKey key, EAction action )
	{
		if( action == EAction::Down && m_ignoreKeys.Exist( key ) )
			return true;

		return ShortcutInputHandler::OnInputEvent( key, action, m_panelID );
	}

	Bool PreviewShortcutInputHandler::OnKeyDown( PreviewPanel&, const InputKeyEvent& evt )
	{
		return InputEventInternal( evt.m_key, EAction::Down );
	}

	Bool PreviewShortcutInputHandler::OnKeyUp( PreviewPanel&, const InputKeyEvent& evt )
	{
		return InputEventInternal( evt.m_key, EAction::Up );
	}

	Bool PreviewShortcutInputHandler::OnMouseDown( PreviewPanel&, const InputMouseButton& evt )
	{
		EInputKey mouseButton = MouseButtonToInputKey( evt.m_button );
		return InputEventInternal( mouseButton, EAction::Down );
	}

	Bool PreviewShortcutInputHandler::OnMouseUp( PreviewPanel&, const InputMouseButton& evt )
	{
		EInputKey mouseButton = MouseButtonToInputKey( evt.m_button );
		return InputEventInternal( mouseButton, EAction::Up );
	}

	Bool PreviewShortcutInputHandler::OnMouseClick( PreviewPanel&, const InputMouseClick& evt )
	{
		EInputKey mouseButton = MouseButtonToInputKey( evt.m_button );
		return InputEventInternal( mouseButton, EAction::Click );
	}

	Bool PreviewShortcutInputHandler::OnMouseDoubleClick( PreviewPanel&, const InputMouseClick& evt )
	{
		EInputKey mouseButton = MouseButtonToInputKey( evt.m_button );
		return InputEventInternal( mouseButton, EAction::DoubleClick );
	}

	Bool PreviewShortcutInputHandler::OnMouseMoved( PreviewPanel&, const InputMouseMove& evt )
	{
		// Not consumed
		return false;
	}

	void PreviewShortcutInputHandler::OnKeyEvent( EInputKey key, Bool keyIsDown )
	{
		if( !KeyStateChange( key, keyIsDown ? EAction::Down : EAction::Up ) && keyIsDown )
			m_ignoreKeys.Insert( key );
		else if( !keyIsDown )
			m_ignoreKeys.Remove( key );
	}
}
