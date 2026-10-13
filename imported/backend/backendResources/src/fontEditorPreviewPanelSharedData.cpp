/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fontEditorPreviewPanelSharedData.h"

namespace tools
{
	FontEditorPreviewPanelSharedData::FontEditorPreviewPanelSharedData( IResourceEditor& editor, const THandle<rend::Font>& font )
		: GenericPreviewPanelSharedData( editor )
		, m_font( font )
	{
	}

	const THandle<rend::Font>& FontEditorPreviewPanelSharedData::GetFont() const
	{
		return m_font;
	}
}	// namespace tools
