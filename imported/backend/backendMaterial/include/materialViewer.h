/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "materialController.h"
#include "../../backendEngine/include/resourceEditor.h"
#include "../../backendEngine/include/previewFwd.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"

namespace tools
{

	class MaterialViewerResourceEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( MaterialViewerResourceEditor );
		DECLARE_INTEROP_DERIVED( MaterialViewerResourceEditor, IResourceEditor );

	public:
		MaterialViewerResourceEditor();
		virtual ~MaterialViewerResourceEditor();

		/// IResourceEditor interface
		virtual Bool Initialize( const THandle< CResource >& resource ) override;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override;
		virtual const THandle< CResource > GetResource() const override;
		virtual void OnPreClose() override;
		void OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel );
		void RefreshPropertyGrid();
		virtual Bool Save( red::DynArray< res::ResourcePath >& successfullySaved = red::DynArray< res::ResourcePath >(),
						   red::DynArray< res::ResourcePath >& failedToSave = red::DynArray< res::ResourcePath >() ) override;

		virtual Bool OnResourcePreSave( const THandle< CResource >& resource ) override;

	private:
		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		MaterialControllerPtr m_materialController;
		red::SharedPtr< GenericPreviewPanelSharedData > m_sharedData;

		SerializableValueNotifier m_serializableNotifier;
	};

} // tools
