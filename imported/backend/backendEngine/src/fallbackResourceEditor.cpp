/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fallbackResourceEditor.h"
#include "genericPreviewPanel.h"

using namespace tools;

RTTI_BEGIN_TYPE_IN_NAMESPACE(FallbackResourceEditor, tools);
	RTTI_PARENT_TYPE(IResourceEditor);
RTTI_END_TYPE();

Bool FallbackResourceEditor::Initialize(const THandle<CResource>& resource)
{
	m_resource = resource;
	return true;
}

const Bool FallbackResourceEditor::SupportsResourceClass(const rtti::ClassType* resourceClass) const
{
	// This class will be picked manually by FindResourceEditorClassByFileExtension function if there is a need to use default fallback editor backend
	return false;
}

const THandle<CResource> FallbackResourceEditor::GetResource() const
{
	return m_resource;
}

void FallbackResourceEditor::SetupInterop(FallbackResourceEditor* ptr, ::interop::utils::Object& op)
{
}

void tools::FallbackResourceEditor::OnPreviewPanelRequest(const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel)
{
	outPanel = red::CreateSharedPtr<GenericPreviewPanel>(renderingWindowPtr, red::CreateSharedPtr<GenericPreviewPanelSharedData>(*this));
}
