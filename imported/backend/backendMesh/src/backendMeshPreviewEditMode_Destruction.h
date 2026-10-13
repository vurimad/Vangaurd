/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "backendMeshPreviewEditMode.h"
#include "backendMeshPanelSharedData.h"

namespace tools
{
	class BACKEND_MESH_API MeshEditModeDestructionMesh final : public MeshPreviewEditMode
	{
	public:
		RED_BASE_CLASS( MeshPreviewEditMode );

		MeshEditModeDestructionMesh();
		virtual ~MeshEditModeDestructionMesh();

		virtual Color GetDisplayColor() const override final;
		virtual Bool OnInitialize( const PreviewPanelSharedDataPtr& sharedData ) override final;
		virtual Bool OnActivate( IPreviewEditMode* currentMode ) override final;
		virtual void OnDeactivate() override final;
		virtual void OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo ) override final;
		virtual void OnStateChanged( const ClothEditorState& state ) {};
		virtual void OnSetupInterop( ::interop::utils::Object& op );
		void OnUpdateSharedState( CName appearanceName, Int32 forcedLOD );
		struct impl;
		red::UniquePtr<impl> pimpl;
	};
}