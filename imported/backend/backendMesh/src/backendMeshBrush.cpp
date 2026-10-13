#include "build.h"
#include "backendMeshBrush.h"
#include "backendMeshPanelSharedData.h"
#include "backendMeshPreviewEditMode_ClothPaint.h"

#include "../../backendEngine/include/renderingWindowCamera.h"
#include "../../backendEngine/include/previewEditMode.h"
#include "../../backendEngine/include/previewInput.h"
#include "../../backendEngine/include/previewPanel.h"
#include "../../backendEngine/include/editActionHistory.h"

#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/renderData/include/renderer.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshParamCloth.h"

#include "../../../common/redReflection/include/singleChannelCurve2.h"
#include "../../../common/redReflection/include/curveInterpolator2.h"

#include "../../../common/generalEditorBackend/include/clothEditorInterop.h"

namespace tools
{
    namespace helper
    {
        Float MapChannelFromGrayScale(Uint8 value)
        {
            const auto result = static_cast<Float>(value) / static_cast<Float>(std::numeric_limits<Uint8>::max());
            return math::Clamp(result, 0.f, 1.f);
        }

        Uint8 MapChannelToGrayScale(Float value)
        {
            return math::Clamp<Uint8>(static_cast<Uint8>(value*255.f), 0, 255); //TODO make this adjustable
        }

        Uint8& GetValueFromChannel(red::BlobSpan channel, Uint32 idx)
        {
            return channel.As<Uint8>(idx);
        }

        Float MapChannelToCM(red::BlobSpan channel, Uint32 idx)
        {
            const Float f = static_cast<Float>(GetValueFromChannel(channel, idx));
            return f * 0.01f;
        }
    }

    struct MeshEditModePaintBrush::impl
    {
        RED_USE_MEMORY_POOL(red::PoolBackend);

        red::DynArray<DebugVertex>* m_paintedVertices;
        red::BlobSpan m_channel;

        red::DynArray<Vector3> m_normals;
        red::DynArray<Float> m_beforePaint;
        red::DynArray<Float> m_t;

        Vector2 m_p = Vector2::ZEROS();

        MeshPanelSharedData* m_data = nullptr;

        Bool m_allowViewportNavigation = false;
        Bool m_ctrlDown = false;
        Bool m_shiftDown = false;
        Bool m_painting = false;
        Bool m_hasChanged = false;
		Bool m_reverseNormals = false;
        
        curve::TSingleChannelCurve<Color> m_colors;
        ClothEditorBrushInfo m_info;
    };

    void Internal_ApplyOpacity(Color& c, Float opacity)
    {
        c.A = math::Clamp<Uint8>(0, 0xFF, static_cast<Uint8>(opacity*255.f));
    }

    void Internal_PaintVertex(MeshEditModePaintBrush::impl& pimpl, Uint8& value, Uint32 idx)
    {
        const Float t = pimpl.m_t[idx];
        value = helper::MapChannelToGrayScale(t);

        auto color = curve::Interpolate(pimpl.m_colors, pimpl.m_t[idx] * pimpl.m_colors.GetNumKeys());
        if(color == Color::BLACK())
            color = Color::MAGENTA();

        Internal_ApplyOpacity(color, pimpl.m_info.m_opacity);
		( *pimpl.m_paintedVertices )[ idx ].colorLinearSpace = color.ToVectorLinearAccu();
    }

    void Internal_PaintVertices(MeshEditModePaintBrush::impl& pimpl)
    {
        for( Uint32 idx : pimpl.m_paintedVertices->Indices() )
        {
            auto& value = helper::GetValueFromChannel(pimpl.m_channel, idx);
            Internal_PaintVertex(pimpl, value, idx);
        }
    }

    struct PaintAction final : IEditAction
    {
        PaintAction(MeshEditModePaintBrush* brush, CName editMode, const red::DynArray<Float>& curr, const red::DynArray<Float>& prev, Bool hasChanged);

        virtual String GetDescription() const override final;
        virtual Bool CanDo() const override final;
        virtual Bool Do() override final;
        virtual Bool Undo() override final;

    private:

        red::DynArray<Float> m_curr;
        red::DynArray<Float> m_prev;
        MeshEditModePaintBrush* m_brush;
        CName m_editMode;
        Bool m_canDo;
    };

    PaintAction::PaintAction(MeshEditModePaintBrush* brush, CName editMode, const red::DynArray<Float>& curr, const red::DynArray<Float>& prev, Bool hasChanged)
        : m_curr(curr)
        , m_prev(prev)
        , m_brush(brush)
        , m_editMode(editMode)
        , m_canDo(hasChanged)
    {}

    String PaintAction::GetDescription() const
    {
        return "Paint";
    }

    Bool PaintAction::CanDo() const
    {
        return m_canDo;
    }

    Bool PaintAction::Do()
    {
        if(m_brush->pimpl->m_data->GetEditModeName() == m_editMode )
        {
            m_brush->pimpl->m_t = m_curr;
            Internal_PaintVertices(*m_brush->pimpl);
            
            return true;
        }

        return false;
    }

    Bool PaintAction::Undo()
    {
        if(m_brush->pimpl->m_data->GetEditModeName() == m_editMode )
        {
            m_brush->pimpl->m_t = m_prev;
            Internal_PaintVertices(*m_brush->pimpl);

            return true;
        }

        return false;
    }

    MeshEditModePaintBrush::MeshEditModePaintBrush(MeshPanelSharedData* data, const curve::TSingleChannelCurve<Color>& colors, red::DynArray<DebugVertex>* vertices, red::DynArray<Vector3> normals, red::BlobSpan channel, Bool reverseNormals)
        : pimpl(red::CreateUniquePtr<impl>())
    {
        pimpl->m_data = data;
        pimpl->m_paintedVertices = vertices;
        pimpl->m_channel = channel;
        pimpl->m_normals = normals;
        pimpl->m_colors = colors;
        pimpl->m_t.Resize(vertices->Size());
		pimpl->m_reverseNormals = reverseNormals;

        for( auto idx : vertices->Indices() )
        {
            pimpl->m_t[idx] = helper::MapChannelFromGrayScale(channel[idx]);
        }
    }

    MeshEditModePaintBrush::~MeshEditModePaintBrush() = default;

    void MeshEditModePaintBrush::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
    {
        rend::FragmentBuilder builder(dd);
        builder.SetColor(Color::LIGHT_GREEN());
        builder.AddFlatCircle(pimpl->m_p, pimpl->m_info.m_radius, 32);

        EditModeViewport vpt(camera, dd);

        for( auto idx : pimpl->m_paintedVertices->Indices() )
        {
            auto n = pimpl->m_normals[idx];
            auto c = camera.GetCameraForward().AsVector3();
            if( c.Dot(n) >= 0.f ) //Ignore normals facing away from us
                continue;

            auto& debugVertex = (*pimpl->m_paintedVertices)[idx];
            Vector3 v{ debugVertex.x, debugVertex.y, debugVertex.z};
            Vector3 screen;
            vpt.WorldToScreen(v, screen);

            const auto rad_sq = pimpl->m_info.m_radius*pimpl->m_info.m_radius;
            const auto length = (screen.AsVector2() - pimpl->m_p).SquareMag();
            auto& value = helper::GetValueFromChannel(pimpl->m_channel, idx);

            if( length < rad_sq )
            {
                const Vector3 min = { -1.f, -1.f, -1.f };
                const Vector3 max = {  1.f,  1.f,  1.f };

                builder.SetColor(Color::LIGHT_GREEN());
                builder.AddSolidBox( {v+min*0.01f}, {v+max*0.01f} );
                const auto numKeys = static_cast<Float>(pimpl->m_colors.GetNumKeys());

                if( !pimpl->m_allowViewportNavigation && pimpl->m_painting)
                {
                    const auto t = (pimpl->m_ctrlDown) ? 255.f - pimpl->m_info.m_fillValue : pimpl->m_info.m_fillValue;
                    pimpl->m_t[idx] = t / 255.f;
                    pimpl->m_hasChanged = true;

                    Internal_PaintVertex(*pimpl, value, idx);
                }
            }

            if( pimpl->m_info.m_drawDistances )
            {
				if( pimpl->m_reverseNormals )
					n *= -1.f;

                builder.SetColor(Color::RED());
                builder.AddLine(v, v + n*helper::MapChannelToCM(pimpl->m_channel, idx) );
            }
        }
    }

    void MeshEditModePaintBrush::OnModifierKeyDown( EInputKey key )
    {
        if( key == IK_Alt )
        {
            pimpl->m_allowViewportNavigation = true;
        }
        else if( key == IK_LControl )
        {
            pimpl->m_ctrlDown = true;
        }
        else if( key == IK_LShift || key == IK_RShift || key == IK_Shift )
        {
            pimpl->m_shiftDown = true;
        }
    }

    Bool MeshEditModePaintBrush::OnKeyUp( PreviewPanel& panel, const InputKeyEvent& evt )
    {
        if( evt.m_key == IK_F )
        {
            auto localBounds = Cast<CMesh>(panel.GetSharedData()->GetEditor().GetResource())->GetBoundingBox();
            panel.FocusOnBounds(localBounds);
            return true;
        }

        return false;
    }

	void MeshEditModePaintBrush::OnModifierKeyUp( EInputKey key )
    {
        if( key == IK_Alt )
        {
            pimpl->m_allowViewportNavigation = false;
        }
        else if( key == IK_LControl )
        {
            pimpl->m_ctrlDown = false;
        }
        else if( key == IK_LShift || key == IK_RShift || key == IK_Shift )
        {
            pimpl->m_shiftDown = false;
        }
    }

    Bool MeshEditModePaintBrush::OnMouseDown( PreviewPanel& panel, const InputMouseButton& evt )
    {
        if( pimpl->m_allowViewportNavigation )
            return false;

        if( evt.m_button == EMouseButton::LMB )
        {
            pimpl->m_painting = true;
            pimpl->m_beforePaint = pimpl->m_t;
        }

        return true;
    }

	Bool MeshEditModePaintBrush::OnMouseUp( PreviewPanel& panel, const InputMouseButton& evt )
    {
        if( pimpl->m_allowViewportNavigation )
            return false;

        if( evt.m_button == EMouseButton::LMB )
        {
            auto action = red::CreateUniquePtr<PaintAction>(this, pimpl->m_data->GetEditModeName(), pimpl->m_t, pimpl->m_beforePaint, pimpl->m_hasChanged);
            panel.GetSharedData()->GetActionHistory().Execute(std::move(action));

            pimpl->m_hasChanged = false;
            pimpl->m_painting = false;
        }

        return true;
    }

    Bool MeshEditModePaintBrush::OnMouseMoved( PreviewPanel& panel, const tools::InputMouseMove& evt )
    {
        if( pimpl->m_allowViewportNavigation )
            return false;

        pimpl->m_p.Set((Float)evt.m_x, (Float)evt.m_y);
        return true;
    }

	Bool MeshEditModePaintBrush::OnMouseWheel( PreviewPanel& panel, const tools::InputMouseWheel& evt )
    {
        if( pimpl->m_allowViewportNavigation )
            return false;

        pimpl->m_info.m_radius += ( evt.m_delta * 10 );
        return true;
    }

    void MeshEditModePaintBrush::Refresh(const ClothEditorBrushInfo& info)
    {
        pimpl->m_info = info;
        Internal_PaintVertices(*pimpl);
    }
}