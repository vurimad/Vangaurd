/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gizmo.h"
#include "gizmoManager.h"
#include "previewInput.h"
#include "previewPanel.h"
#include "previewEditMode.h"
#include "previewPanelSharedData.h"
#include "editActionHistory.h"
#include "../../../common/engine/include/renderFrame.h"

namespace tools
{
	static const CName c_gizmoContextName = RED_NAME_CONSTEXPR( "gizmo" );

	GizmoManager::GizmoManager()
		: m_gizmos { red::PoolBackend() }
		, m_active( nullptr )
		, m_isDuringAction( false )
		, m_stopTransactionOnInputListenerDetach( false )
	{
	}

	GizmoManager::~GizmoManager() = default;

	void GizmoManager::RegisterShortcuts( const red::SharedPtr< ShortcutManager >& manager, PreviewPanel* panel )
	{
		m_shortcutManager = manager;

		// Clone
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_LeftMouse, EAction::Down ).Modifier( EInputKey::IK_LShift );

			ShortcutCallback callback = [ this, panel ]( const KeyEventData& data )
			{
				return OnGizmoDragAction( panel, panel->GetMouseX(), panel->GetMouseY() );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idGizmoDrag" ).Binding( binding ).Callback( callback )
				.Context( c_gizmoContextName ).Name( "Clone objects" ).Description( "Clone selected objects" );
			manager->Register( shortcut );
		}

		// Move Objects Pivots
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_LeftMouse, EAction::Down ).Key( EInputKey::IK_P );

			ShortcutCallback callback = [ this, panel ]( const KeyEventData& data )
			{
				return OnTransformObjectsPivotsAction( *panel );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idPivotMove" ).Binding( binding ).Callback( callback )
				.Context( c_gizmoContextName ).Name( "Move Object Pivot" ).Description( "Move the pivot of the selected objects" );
			manager->Register( shortcut );
		}

		// Transform objects individually
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_LeftMouse, EAction::Down ).Key( EInputKey::IK_I );

			ShortcutCallback callback = [this, panel] ( const KeyEventData& data )
			{
				return OnTransformObjectsIndividuallyAction( *panel );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idTransformIndividually" ).Binding( binding ).Callback( callback )
				.Context( c_gizmoContextName ).Name( "Transform Individually" ).Description( "Transform object individually (relatively to their pivots)" );
			manager->Register( shortcut );
		}
	}

	void GizmoManager::Register( IGizmo* gizmo )
	{
		if ( gizmo )
		{
			RED_ASSERT( !m_gizmos.Exist( gizmo ), "The same gizmo registered twice" );
			red::alg::PushBackUnique( m_gizmos, gizmo );
		}
	}

	void GizmoManager::Unregister( IGizmo* gizmo )
	{
		if ( gizmo == m_active )
		{
			m_active = nullptr;
			m_shortcutManager->PopContext( c_gizmoContextName );
		}

		m_gizmos.Remove( gizmo );
	}

	CName GizmoManager::GetName() const
	{
		static CName name = RED_NAME_CONSTEXPR( "GizmoManager" );
		return name;
	}

	void GizmoManager::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{
		// render all non-active gizmos
		for ( auto* ptr : m_gizmos )
		{
			if ( ptr != m_active )
			{
				if ( ptr->OnCanRender( camera ) )
				{
					const auto mode = m_isDuringAction ? IGizmo::ERenderMode::OtherActive : IGizmo::ERenderMode::Normal;
					ptr->OnRender( camera, dd, mode );
				}
			}
		}

		// render selected gizmo
		if ( m_active != nullptr )
		{
			const auto mode = m_isDuringAction ? IGizmo::ERenderMode::Active : IGizmo::ERenderMode::Selected;
			m_active->OnRender( camera, dd, mode );
		}
	}

	// This is a tempfix solution while we lack proper mouse drag functionality
	// Instead of performing the requested action immediately (such as clone object)
	// this handler will instead wait for the mouse to be dragged a short distance before
	// switching to the intended action and input handler
	class DragDelayInputHandler : public IPreviewInputHandler
	{
	public:
		using DragCompleteCallback = std::function< void( PreviewPanel*, Int32, Int32 ) >;

		DragDelayInputHandler( PreviewPanel* previewPanel, const Int32 mouseX, const Int32 mouseY, DragCompleteCallback&& callback )
			: m_previewPanel( previewPanel )
			, m_mouseStart( static_cast< Float >( mouseX ), static_cast< Float >( mouseY ) )
			, m_callback( callback )
		{
		}

		virtual ~DragDelayInputHandler() = default;

		virtual Bool OnMouseMoved( const InputMouseMove& evt ) override
		{
			math::Vector2 mouseNow( static_cast< Float >( evt.m_x ), static_cast< Float >( evt.m_y ) );

			Float distance = m_mouseStart.DistanceTo( mouseNow );

			if( distance > 5.0f )
			{
				m_callback( m_previewPanel, evt.m_x, evt.m_y );

				// The callback should have switched to another input handler
				// So we must not return false here or we will deregister the new handler
			}

			return true;
		}

		virtual Bool OnKeyUp( const InputKeyEvent& evt ) override
		{
			return false;
		}


		virtual Bool OnMouseUp( const InputMouseButton& evt ) override
		{
			return evt.m_button != LMB;
		}

	private:
		PreviewPanel* m_previewPanel;
		const math::Vector2 m_mouseStart;
		DragCompleteCallback m_callback;
	};

	Bool GizmoManager::OnGizmoDragAction( PreviewPanel* panel, Int32 mouseX, Int32 mouseY )
	{
		if ( m_active != nullptr && m_active->SupportsCloning() && OnClone )
		{
			red::UniquePtr< IPreviewInputHandler > handler = red::CreateUniquePtr< DragDelayInputHandler >( panel, mouseX, mouseY, [ this, mouseX, mouseY ]( PreviewPanel* previewPanel, Int32, Int32 )
			{
				OnGizmoDragActionConfirmed( *previewPanel, mouseX, mouseY );
			} );

			panel->AttachInputHandler( std::move( handler ) );

			return true;
		}

		return false;
	}

	Bool GizmoManager::OnGizmoDragActionConfirmed( PreviewPanel& panel, Int32 mouseX, Int32 mouseY )
	{
		if ( m_active && m_active->SupportsCloning() )
		{
			if ( OnClone )
			{
				panel.GetSharedData()->GetActionHistory().StartTransaction( "Clone Object" );

				OnClone();

				EditModeViewport emv;
				panel.SetupEditModeViewport( emv );
				UpdateSelection( panel, mouseX, mouseY, emv );

				if( !m_active )
				{
					panel.GetSharedData()->GetActionHistory().EndTransaction();

					return true;
				}

				// create input handler to track the gizmo
				red::UniquePtr< IPreviewInputHandler > handler = m_active->OnActivate( GizmoTransformMode::Objects );

				// No OnActivate() implementation returns nullptr, so 
				RED_FATAL_ASSERT( handler, "Missing IPreviewInputHandler in clone operation" );

				// valid input handler created by the gizmo
				panel.AttachInputHandler( std::move( handler ) );
				m_isDuringAction = true;

				UpdateSelection( panel, mouseX, mouseY, emv );

				m_stopTransactionOnInputListenerDetach = true;

				return true;
			}

		}

		return false;
	}

	Bool GizmoManager::OnTransformObjectsPivotsAction( PreviewPanel& panel )
	{
		if ( m_active != nullptr && m_active->SupportsPivotMovement() )
		{
			if ( red::UniquePtr< IPreviewInputHandler > handler = m_active->OnActivate( GizmoTransformMode::ObjectsPivots ) )
			{
				// valid input handler created by the gizmo
				panel.AttachInputHandler( std::move( handler ) );
				m_isDuringAction = true;

				return true;
			}
		}
		return false;
	}

	Bool GizmoManager::OnTransformObjectsIndividuallyAction( PreviewPanel& panel )
	{
		if ( m_active != nullptr )
		{
			// create input handler to track the gizmo
			red::UniquePtr< IPreviewInputHandler > handler = m_active->OnActivate( GizmoTransformMode::ObjectsIndividually );
			if ( handler )
			{
				panel.AttachInputHandler( std::move( handler ) );
				m_isDuringAction = true;

				return true;
			}
		}

		return false; // not handled
	}

	Bool GizmoManager::OnMouseDown( PreviewPanel& panel, const InputMouseButton& evt )
	{
		if ( evt.m_button == LMB && evt.m_mask == LMB ) // strictly LMB (with no other buttons pressed)
		{
			if ( m_active != nullptr )
			{
				// create input handler to track the gizmo
				red::UniquePtr< IPreviewInputHandler > handler = m_active->OnActivate( GizmoTransformMode::Objects );
				if ( handler )
				{
					// valid input handler created by the gizmo
					panel.AttachInputHandler( std::move( handler ) );
					m_isDuringAction = true;

					return true;
				}
			}
		}

		return false; // not handled
	}

	void GizmoManager::OnInputHandlerRemoved( PreviewPanel& panel, IPreviewInputHandler* handler, Bool wasCancelled )
	{
		if( m_stopTransactionOnInputListenerDetach )
		{
			IEditActionHistory& actionHistory = panel.GetSharedData()->GetActionHistory();

			actionHistory.EndTransaction();

			m_stopTransactionOnInputListenerDetach = false;

			if( wasCancelled )
			{
				actionHistory.CancelTopUndoAction();
			}
		}

		if ( m_active != nullptr )
		{
			m_isDuringAction = false;
			m_active = nullptr;

			m_shortcutManager->PopContext( c_gizmoContextName );
		}
	}

	Bool GizmoManager::OnMouseMoved( PreviewPanel& panel, const InputMouseMove& evt )
	{
		UpdateSelection( panel, evt.m_x, evt.m_y, evt.m_viewport );
		return false; // input is transparent
	}

	void GizmoManager::UpdateSelection( PreviewPanel& panel, Int32 x, Int32 y, const EditModeViewport& vpt )
	{
		const IGizmo* oldActiveGizmo = m_active;

		// find all gizmos that collide with mouse cursor
		red::DynArray< IGizmo* > gizmosUnderMouse{ red::PoolBackend() };
		IGizmo::Priority currentPriority = IGizmo::Priority_Low;

		for ( auto ptr : m_gizmos )
		{
			if ( !ptr->OnCanRender( vpt.m_camera ) )
				continue;

			IGizmo::Priority priority = IGizmo::Priority_Low;
			if ( !ptr->OnCheckCollision( x, y, vpt, priority ) )
				continue; 

			if ( priority > currentPriority )
			{
				gizmosUnderMouse.Clear();
				currentPriority = priority;
			}

			if ( priority == currentPriority )
			{
				gizmosUnderMouse.PushBack( ptr );
			}
		}

		// there is no gizmos colliding with cursor
		if ( gizmosUnderMouse.Empty() )
		{
			m_active = nullptr;
		}
		else
		{
			Float depth = std::numeric_limits<Float>::max();
			IGizmo* activeCandidate = nullptr;

			// amongst gizmos that collide with cursor find the one that is closest to camera
			for ( IGizmo* gizmo : gizmosUnderMouse )
			{
				Float max, min, mid;
				gizmo->GetDepth( vpt, min, max );
				mid = 0.5f * ( min + max );

				if ( mid < depth )
				{
					activeCandidate = gizmo;
					depth = mid;
				}
			}
			m_active = activeCandidate;
		}

		// update shortcut manager context
		if ( m_active && !oldActiveGizmo )
		{
			m_shortcutManager->PushContext( c_gizmoContextName, ShortcutContextMode::Inherit );
		}
		else if ( !m_active && oldActiveGizmo )
		{
			m_shortcutManager->PopContext( c_gizmoContextName );
		}
	}
}
