/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../common/generalEditorBackend/include/generalEditorBackendPublic.h"

#include "previewInputListener.h"
#include "previewInput.h"

namespace tools
{
	class BACKEND_ENGINE_API PreviewShortcutInputHandler : public ShortcutInputHandler, public IPreviewInputListener
	{
	public:
		PreviewShortcutInputHandler( const Uint32 panelID );
		virtual ~PreviewShortcutInputHandler() override;

	private:
		EInputKey MouseButtonToInputKey( EMouseButton button ) const;

		Bool InputEventInternal( EInputKey key, EAction action );

		virtual Bool OnKeyDown( PreviewPanel&, const InputKeyEvent& evt ) override final;
		virtual Bool OnKeyUp( PreviewPanel&, const InputKeyEvent& evt ) override final;
		virtual Bool OnMouseDown( PreviewPanel&, const InputMouseButton& evt ) override final;
		virtual Bool OnMouseUp( PreviewPanel&, const InputMouseButton& evt ) override final;

		virtual Bool OnMouseClick( PreviewPanel&, const InputMouseClick& evt ) override final;
		virtual Bool OnMouseDoubleClick( PreviewPanel&, const InputMouseClick& evt ) override final;
		virtual Bool OnMouseMoved( PreviewPanel&, const InputMouseMove& evt ) override final;

		virtual void OnKeyEvent( EInputKey key, Bool keyIsDown ) override final;

		red::Set< EInputKey > m_ignoreKeys;
		Uint32 m_panelID;
	};
}
