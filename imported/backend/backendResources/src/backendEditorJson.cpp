/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendEditorJson.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( JsonEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	EmptyPreviewPanel::EmptyPreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedData )
		: PreviewPanel( renderingWindow, sharedData )
	{
	}

	void EmptyPreviewPanel::SetupInterop( EmptyPreviewPanel* previewPanel, ::interop::utils::Object& op )
	{
	}

	JsonEditor::JsonEditor()
		: m_sharedData( RED_NEW(PreviewPanelSharedData)( *this ))
	{
	}

	Bool JsonEditor::Initialize( const THandle< CResource >& resource )
	{
		return static_cast< Bool >( m_resource = Cast< JsonResource >( resource ) );
	}

	const Bool JsonEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA< JsonResource  >();
	}

	const THandle< CResource > JsonEditor::GetResource() const
	{
		return m_resource;
	}

	void JsonEditor::OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel )
	{
		outPanel = red::CreateSharedPtr<EmptyPreviewPanel>( renderingWindowPtr, m_sharedData );
	}

	void JsonEditor::SetupInterop( JsonEditor* editor, ::interop::utils::Object& op )
	{
	}
}  // namespace tools