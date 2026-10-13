/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/resourceEditor.h"
#include "../../backendEngine/include/previewFwd.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"

#include "mlmaskPreviewPanel.h"
#include "mlmaskView.h"


namespace tools
{

	class MLMaskViewerResourceEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( MLMaskViewerResourceEditor );
		DECLARE_INTEROP_DERIVED( MLMaskViewerResourceEditor, IResourceEditor );

	public:
		MLMaskViewerResourceEditor();
		virtual ~MLMaskViewerResourceEditor();

		/// IResourceEditor interface
		virtual Bool Initialize( const THandle< CResource >& resource ) override;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override;
		virtual const THandle< CResource > GetResource() const override;
		virtual void OnPreClose() override;
		void OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel );
		void OnPreviewParameterChanged();

		THandle< Multilayer_Mask > GetMLMask() const { return m_resource; }

	private:
		virtual Bool Save( red::DynArray< res::ResourcePath >& successfullySaved = red::DynArray< res::ResourcePath >(),
						   red::DynArray< res::ResourcePath >& failedToSave = red::DynArray< res::ResourcePath >() ) override final;

		THandle< Multilayer_Mask > m_resource;
		red::SharedPtr< MLMaskController > m_controller;
		red::SharedPtr< GenericPreviewPanelSharedData > m_sharedData;
		red::SharedPtr< MLMaskPreviewPanel > m_maskPreviewPanel;

		SerializableValueNotifier m_serializableNotifier;
	};

} // tools
