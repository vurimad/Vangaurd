/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "backendMeshPreviewEditMode.h"
#include "backendMeshPanelSharedData.h"

namespace tools
{
    struct MeshEditModePaintBrush;
    struct ClothEditorState;
    struct ClothEditorBrushInfo;

    class BACKEND_MESH_API MeshEditModeClothPaint final : public MeshPreviewEditMode
	{
	public:
        RED_BASE_CLASS(MeshPreviewEditMode);

		MeshEditModeClothPaint();
		virtual ~MeshEditModeClothPaint();

        virtual Color GetDisplayColor() const override final;
        virtual Bool OnInitialize(const PreviewPanelSharedDataPtr& sharedData) override final;
        virtual Bool OnActivate( IPreviewEditMode* currentMode ) override final;
        virtual void OnDeactivate() override final;
		virtual void OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo ) override final;
        virtual void OnStateChanged(const ClothEditorState& state) override final;

        void ChangeBrush(Int32 brushIndex, Uint16 chunk);
        void RefreshMaxDistance(const tools::ClothEditorBrushInfo& info, Uint16 chunk);
		void RefreshMaxDistanceExt(const tools::ClothEditorBrushInfo& info, Uint16 chunk);
        void RefreshBackstopDistance(const tools::ClothEditorBrushInfo& info, Uint16 chunk);
		void RefreshBackstopRadius(const tools::ClothEditorBrushInfo& info, Uint16 chunk);
        void RefreshSelfCollision(const tools::ClothEditorBrushInfo& info, Uint16 chunk);

        struct impl;
        red::UniquePtr<impl> pimpl;

    private:
        void Internal_AttachInputListener(IPreviewInputListener* listener);
        void Internal_DetachInputListener(IPreviewInputListener* listener);
        void Internal_ChangeBrush(MeshEditModePaintBrush* brush, const ClothEditorBrushInfo& prevInfo);

        red::DynArray<red::UniquePtr<MeshEditModePaintBrush>> m_maxDistanceBrush;
		red::DynArray<red::UniquePtr<MeshEditModePaintBrush>> m_maxDistanceExtBrush;
        red::DynArray<red::UniquePtr<MeshEditModePaintBrush>> m_backstopDistanceBrush;
		red::DynArray<red::UniquePtr<MeshEditModePaintBrush>> m_backstopRadiusBrush;
        red::DynArray<red::UniquePtr<MeshEditModePaintBrush>> m_selfCollisionBrush;
	};
}