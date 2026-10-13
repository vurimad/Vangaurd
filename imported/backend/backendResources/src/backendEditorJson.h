/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/resourceEditor.h"
#include "../../backendEngine/include/previewPanel.h"
#include "../../backendEngine/include/previewPanelSharedData.h"
#include "../../../common/resource/include/jsonResource.h"

namespace tools
{
	class EmptyPreviewPanel : public PreviewPanel
	{
		DECLARE_INTEROP_DERIVED( EmptyPreviewPanel, PreviewPanel );

	public:
		EmptyPreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedData );

	};

	class JsonEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( JsonEditor );
		DECLARE_INTEROP_DERIVED( JsonEditor, IResourceEditor );

		JsonEditor();

		// Interface implementation
		virtual Bool Initialize( const THandle< CResource >& resource ) override;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override;
		virtual const THandle< CResource > GetResource() const override;
		virtual void OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel ) override;

	private:
		THandle< JsonResource >	m_resource;
		PreviewPanelSharedDataPtr m_sharedData;
	};
}  // namespace tools