/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "backendMeshPreviewEditMode.h"
#include "../../../common/redReflection/include/resourceMonitor.h"

namespace world
{
    class RuntimeSystemRendering;
}

namespace tools
{
    struct MeshEditModeClothSimulation_InputListener;
    struct ClothEditorState;

    class BACKEND_MESH_API MeshEditModeClothSimulation final : public MeshPreviewEditMode
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
	public:
        RED_BASE_CLASS(GenericEditMode<MeshPanelSharedData>);

		MeshEditModeClothSimulation();
		virtual ~MeshEditModeClothSimulation();

        virtual Color GetDisplayColor() const override final;
        virtual Bool OnInitialize(const PreviewPanelSharedDataPtr& sharedData) override final;
        virtual Bool OnActivate( IPreviewEditMode* currentMode ) override final;
        virtual void OnDeactivate() override final;
		virtual void OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo ) override final;
		virtual void OnUpdate( const UpdateContext& ctx ) override final;
        virtual void OnStateChanged(const ClothEditorState& state) override final;
        virtual void OnSetupInterop( ::interop::utils::Object& op ) override final;

        struct impl;
        red::UniquePtr<impl> pimpl;

    private:
        void Internal_SetupMeshForRendering();
        void Internal_RefreshPhysicsProxy();

        Bool Internal_ClothProxyPush(Uint8 lod, red::ArraySpan<const Vector3>& positions, red::ArraySpan<const Vector3>& normals);
        void Internal_ClothProxyPull(Uint8 lod, red::ArraySpan<const Vector4> positions, Box bounds);

		void Internal_OnResourceMonitorMessage(const res::ResourcePath& path, const CName& message, const res::MonitorUserDataPtr& userData);

        red::UniquePtr<MeshEditModeClothSimulation_InputListener> m_inputListener;
		res::Monitor m_resourceMonitor;
	};
}