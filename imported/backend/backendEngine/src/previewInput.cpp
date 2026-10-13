/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewInput.h"
#include "previewInputHandler.h"

namespace tools
{
	void InputKeyModifiers::UpdateKey(EInputKey key, bool keyIsDown)
	{
		switch (key)
		{
		case IK_LShift:   m_flags = (keyIsDown ? (m_flags | LShift) :   (m_flags & ~static_cast<Uint8>(LShift)));   break;
		case IK_RShift:   m_flags = (keyIsDown ? (m_flags | RShift) :   (m_flags & ~static_cast<Uint8>(RShift)));   break;
		case IK_LControl: m_flags = (keyIsDown ? (m_flags | LControl) : (m_flags & ~static_cast<Uint8>(LControl))); break;
		case IK_RControl: m_flags = (keyIsDown ? (m_flags | RControl) : (m_flags & ~static_cast<Uint8>(RControl))); break;
		case IK_Alt:      m_flags = (keyIsDown ? (m_flags | LAlt) :     (m_flags & ~static_cast<Uint8>(LAlt)));     break;
		default:          break;
		}
	}

	Bool InputKeyModifiers::IsModifierKey( EInputKey key )
	{
		switch ( key )
		{
		case IK_Shift:
		case IK_LShift:
		case IK_RShift:
		case IK_Ctrl:
		case IK_LControl:
		case IK_RControl:
		case IK_Alt:
			return true;

		default:
			return false;
		}
	}
}
