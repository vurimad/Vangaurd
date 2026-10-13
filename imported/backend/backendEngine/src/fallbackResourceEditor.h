/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "resourceEditor.h"

namespace tools
{
	// If none of the IResourceEditor implementation can handle given resource, then this fallback editor backend will be used
	class FallbackResourceEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE(FallbackResourceEditor);
		DECLARE_INTEROP_DERIVED(FallbackResourceEditor, IResourceEditor);

	public:
		virtual Bool Initialize(const THandle<CResource>& resource) override;
		virtual const Bool SupportsResourceClass(const rtti::ClassType* resourceClass) const override;
		virtual const THandle<CResource> GetResource() const override;
		virtual void OnPreviewPanelRequest(const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel) override;

	private:
		THandle<CResource> m_resource;
	};
}
