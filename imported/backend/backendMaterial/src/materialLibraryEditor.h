/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "materialController.h"
#include "../../backendEngine/include/resourceEditor.h"


class CMaterialLayerLibrary;

namespace tools
{
	class GenericPreviewPanelSharedData;

	class MaterialLibraryEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( MaterialLibraryEditor );
		DECLARE_INTEROP_DERIVED( MaterialLibraryEditor, IResourceEditor );

	public:

		MaterialLibraryEditor();
		virtual ~MaterialLibraryEditor();

		virtual bool Initialize(const THandle< CResource >& resource) override final;
		virtual const bool SupportsResourceClass(const rtti::ClassType* resourceClass) const override final;
		virtual const THandle< CResource > GetResource() const override final;
		virtual void OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel ) override final;

	private:

		THandle< CMaterialLayerLibrary > m_materialLib;
		red::SharedPtr< GenericPreviewPanelSharedData > m_sharedData;
		MaterialControllerPtr m_materialController;
	};
}
