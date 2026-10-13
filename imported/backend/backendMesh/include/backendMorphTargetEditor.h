/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../backendEngine/include/resourceEditor.h"

class MorphTargetMesh;

namespace tools
{
	class GenericPreviewPanelSharedData;
	class MorphTargetPreviewPanel;

	struct UpdateContext;

	class BACKEND_MESH_API MorphTargetResourceEditor: public IResourceEditor
	{
		RED_BASE_CLASS( IResourceEditor );
		RTTI_DECLARE_TYPE( MorphTargetResourceEditor );
		DECLARE_INTEROP_DERIVED( MorphTargetResourceEditor, IResourceEditor );

	public:
		MorphTargetResourceEditor();
		virtual ~MorphTargetResourceEditor();

		/// IResourceEditor interface
		virtual Bool Initialize( const THandle< CResource >& resource ) override;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override;
		virtual const THandle< CResource > GetResource() const override;
		virtual void OnPreviewPanelRequest( const String& type, tools::RenderingWindowPtr renderingWindowPtr, tools::PreviewPanelPtr& outPanel ) override;

		void OnUpdate( const UpdateContext& ctx );

	protected:
		virtual void OnActionExecute( const IEditAction& action ) override;
		void OnResourceModified( Bool shouldRecreateRenderProxy = false );
		Bool CachePreviewTargets();											// returns true if targets count changed

	private:
		THandle< MorphTargetMesh >	m_resource;
		red::DynArray< Float >		m_previewTargets;
		CName						m_appearance;

		red::SharedPtr< GenericPreviewPanelSharedData > m_previewSharedData;
		red::SharedPtr< MorphTargetPreviewPanel >		m_previewPanel;
	};
}

