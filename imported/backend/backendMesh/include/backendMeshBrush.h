/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../backend/backendEngine/include/editAction.h"
#include "../../../backend/backendEngine/include/previewInputListener.h"

namespace curve
{
    template< typename T >
    struct TSingleChannelCurve;
}

namespace tools
{
    class MeshPanelSharedData;
    struct ClothEditorBrushInfo;

    struct MeshEditModePaintBrush final : public IPreviewInputListener
    {
        RED_BASE_CLASS(IPreviewInputListener);

        MeshEditModePaintBrush(MeshPanelSharedData* data, const curve::TSingleChannelCurve<Color>& colors, red::DynArray<DebugVertex>* vertices, red::DynArray<Vector3> normals, red::BlobSpan channel, Bool reverseNormals);
        ~MeshEditModePaintBrush();

        virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override final;
        virtual void OnModifierKeyDown( EInputKey key ) override final;
		virtual void OnModifierKeyUp( EInputKey key ) override final;
        virtual Bool OnKeyUp( PreviewPanel& panel, const InputKeyEvent& evt ) override final;
        virtual Bool OnMouseDown( PreviewPanel& panel, const InputMouseButton& evt ) override final;
		virtual Bool OnMouseUp( PreviewPanel& panel, const InputMouseButton& evt ) override final;
        virtual Bool OnMouseMoved( PreviewPanel& panel, const InputMouseMove& evt ) override final;
		virtual Bool OnMouseWheel( PreviewPanel& panel, const InputMouseWheel& evt ) override final;

        void Refresh(const ClothEditorBrushInfo& info);

        struct impl;
        red::UniquePtr<impl> pimpl;
    };
}