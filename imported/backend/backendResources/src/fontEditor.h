/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/resourceEditor.h"
#include "../../backendEngine/include/previewPanel.h"

namespace rend
{
	class Font;
}

namespace tools
{
	class FontEditorPreviewPanelSharedData;

	class FontEditor final : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( FontEditor );
		DECLARE_INTEROP_DERIVED( FontEditor, IResourceEditor );
		RED_BASE_CLASS( IResourceEditor );

	public:
		FontEditor();

		virtual Bool Initialize( const THandle< CResource >& resource ) override;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override;
		virtual const THandle< CResource > GetResource() const override;
		virtual void OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel ) override;

	private:
		THandle< rend::Font > m_resource;
		red::SharedPtr< FontEditorPreviewPanelSharedData > m_sharedData;
	};

}	// namespace tools
