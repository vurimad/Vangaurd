/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fontEditorPreviewPanel.h"
#include "fontEditorPreviewPanelSharedData.h"
#include "fontEditor.h"
#include "../../../common/renderData/include/font.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( FontEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	FontEditor::FontEditor()
	{
	}

	Bool FontEditor::Initialize( const THandle< CResource >& resource )
	{
		THandle<rend::Font> fontResource = Cast< rend::Font >( resource );
		if ( !fontResource )
		{
			return false;
		}

		m_sharedData = red::CreateSharedPtr< FontEditorPreviewPanelSharedData >( *this, fontResource );

		m_resource = fontResource;

		return true;
	}

	const Bool FontEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA< rend::Font >();
	}

	const THandle< CResource > FontEditor::GetResource() const
	{
		return m_resource;
	}

	void FontEditor::OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel )
	{
		outPanel = red::CreateSharedPtr<FontEditorPreviewPanel>( renderingWindowPtr, m_sharedData );
	}

	void FontEditor::SetupInterop( FontEditor* editor, ::interop::utils::Object& op )
	{
	}

}	// namespace tools
