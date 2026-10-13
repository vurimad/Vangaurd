/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/genericPreviewPanelSharedData.h"

namespace rend
{
	class Font;
}

namespace tools
{
	class FontEditorPreviewPanelSharedData : public GenericPreviewPanelSharedData
	{
	public:
		FontEditorPreviewPanelSharedData( IResourceEditor& editor, const THandle<rend::Font>& font);

		const THandle<rend::Font>& GetFont() const;

	private:
		THandle<rend::Font> m_font;
	};

}	// namespace tools
