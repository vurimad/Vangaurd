/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/resourceEditor.h"
#include "../../backendEngine/include/previewFwd.h"

#include "../../../common/redReflection/include/serializableValueNotifier.h"

#include "texturePreviewPanel.h"
#include "textureView.h"


namespace tools
{

	class TextureViewerResourceEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( TextureViewerResourceEditor );
		DECLARE_INTEROP_DERIVED( TextureViewerResourceEditor, IResourceEditor );

	public:
		TextureViewerResourceEditor();
		virtual ~TextureViewerResourceEditor();

		/// IResourceEditor interface
		virtual Bool Initialize( const THandle< CResource >& resource ) override;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override;
		virtual const THandle< CResource > GetResource() const override;
		virtual void OnPreClose() override;
		void OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel );

	private:
		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );
		virtual Bool Save( red::DynArray< res::ResourcePath >& successfullySaved = red::DynArray< res::ResourcePath >(),
						   red::DynArray< res::ResourcePath >& failedToSave = red::DynArray< res::ResourcePath >() ) override final;
		Bool CookTexture();

		THandle< ITexture > m_resource;
		red::SharedPtr< TextureController > m_controller;
		red::SharedPtr< GenericPreviewPanelSharedData > m_sharedData;
		red::SharedPtr< TexturePreviewPanel > m_texturePreviewPanel;

		SerializableValueNotifier m_serializableNotifier;
	};

} // tools
