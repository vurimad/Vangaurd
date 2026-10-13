/**
* Copyright (c) 2013-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewPanel.h"

#include "selectionHandler.h"
#include "selectionInputListener.h"
#include "selectionScreenAreaInputHandler.h"

namespace tools
{
	SelectionInputListener::SelectionInputListener( SelectionHandler& target )
		: m_target( target )
		, m_ignoreSelection( true )
		, m_marqueeSelectsThroughObjects( false )
	{
	}

	SelectionInputListener::~SelectionInputListener() = default;

	void SelectionInputListener::RegisterShortcuts( ShortcutManager* manager, PreviewPanel* panel )
	{
		// Single selection
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_LeftMouse );

			ShortcutCallback callback = [ this, panel ]( const KeyEventData& data )
			{
				return OnSelectionAction( *panel, panel->GetMouseX(), panel->GetMouseY() );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idPreviewSelection" ).Binding( binding ).Callback( callback );

			manager->Register( shortcut );
		}

		// Multi selection
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_LeftMouse ).Modifier( EInputKey::IK_LControl );

			ShortcutCallback callback = [ this, panel ]( const KeyEventData& data )
			{
				return OnMultiselectionAction( *panel, panel->GetMouseX(), panel->GetMouseY() );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idMultiSelection" ).Binding( binding ).Callback( callback );

			manager->Register( shortcut );
		}

		// Area selection
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_LeftMouse, EAction::Down ).Modifier( EInputKey::IK_LShift );

			ShortcutCallback callback = [ this, panel ]( const KeyEventData& data )
			{
				if ( panel->IsMouseCursorOverPreviewPanel() )
				{
					return OnAdditiveAreaSelectionAction( *panel, panel->GetMouseX(), panel->GetMouseY() );
				}
				return false;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idAreaSelection" ).Binding( binding ).Callback( callback ).Name( "Area selection (additive)" );

			manager->Register( shortcut );
		}

		// Area deselection
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_LeftMouse, EAction::Down ).Modifier( EInputKey::IK_LControl ).Modifier( EInputKey::IK_LShift );

			ShortcutCallback callback = [ this, panel ]( const KeyEventData& data )
			{
				return OnSubtractiveAreaSelectionAction( *panel, panel->GetMouseX(), panel->GetMouseY() );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idSubtractiveAreaSelection" ).Binding( binding ).Callback( callback ).Name( "Area selection (subtractive)" );

			manager->Register( shortcut );
		}

		// Clear selection
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_D ).Modifier( EInputKey::IK_LControl );

			ShortcutCallback callback = [ this ]( const KeyEventData& data )
			{
				return OnClearSelectionAction();
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idClearSelection" ).Binding( binding ).Callback( callback ).Name( "Clear selection" ).Description( "Deselect everything" );

			manager->Register( shortcut );
		}

		// Hide selection
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_H ).Modifier( EInputKey::IK_LControl );

			ShortcutCallback callback = [ this ]( const KeyEventData& data )
			{
				return OnHideSelectionAction();
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idHideSelection" ).Binding( binding ).Callback( callback ).Name( "Hide selection" ).Description( "Hide the selected nodes in the preview window" );

			manager->Register( shortcut );
		}
	}

	void SelectionInputListener::SetupInterop( SelectionInputListener* ptr, ::interop::utils::Object& op )
	{
		op.RegisterValue<Bool>( "marqueeSelectsThroughObjects",
			/* GET */ [ptr] ( interop::IContext& ctx, Bool& outValue )
			{
				outValue = ptr->m_marqueeSelectsThroughObjects;
			},
			/* SET */ [ptr] ( interop::IContext& ctx, const Bool& newValue )
			{
				ptr->m_marqueeSelectsThroughObjects = newValue;
			},
			&ptr->m_marqueeSelectsThroughObjectsNotifier );

		op.RegisterFunctor< void >( "toggleMarqueeSelectsThroughObjects", [ptr] ( ::interop::IContext& )
		{
			ptr->m_marqueeSelectsThroughObjects = !ptr->m_marqueeSelectsThroughObjects;
			ptr->m_marqueeSelectsThroughObjectsNotifier.Notify( String::EMPTY() );
		} );
	}

	Bool SelectionInputListener::ShouldIgnoreSelection() const
	{
		return m_ignoreSelection;
	}

	Uint32 SelectionInputListener::GetDefaultSelectionDepth() const
	{
		return 0;
	}

	Bool SelectionInputListener::OnSelectionAction( PreviewPanel& panel, Int32 mouseX, Int32 mouseY )
	{
		if ( !ShouldIgnoreSelection() )
		{
			SelectionContext context( panel, mouseX, mouseY );
			context.m_multlayer = GetSelectionHandler().IsMultilayerSelectionEnabled();
			m_target.PerformSelection( context );
		}

		return false; // input is transparent
	}

	Bool SelectionInputListener::OnMultiselectionAction( PreviewPanel& panel, Int32 mouseX, Int32 mouseY )
	{
		if ( !ShouldIgnoreSelection() )
		{
			SelectionContext context( panel, mouseX, mouseY );
			context.m_mode = SelectionMode::Invert;
			context.m_multlayer = GetSelectionHandler().IsMultilayerSelectionEnabled();
			m_target.PerformSelection( context );
		}

		return false; // input is transparent
	}

	Bool SelectionInputListener::OnAdditiveAreaSelectionAction( PreviewPanel& panel, Int32 mouseX, Int32 mouseY )
	{
		// start area selection action
		// Note: we cannot use custom selection depth (like in WorldSelectionInputListener) as we may select objects at various depths
		const SelectionContext context( panel, mouseX, mouseY, GetDefaultSelectionDepth(), SelectionMode::Add, m_marqueeSelectsThroughObjects || GetSelectionHandler().IsMultilayerSelectionEnabled() );
		panel.AttachInputHandler( red::CreateUniquePtr< SelectionScreenAreaInputHandler >( m_target, mouseX, mouseY, context ) );

		return true;
	}

	Bool SelectionInputListener::OnSubtractiveAreaSelectionAction( PreviewPanel& panel, Int32 mouseX, Int32 mouseY )
	{
		// start area selection action
		// Note: we cannot use custom selection depth (like in WorldSelectionInputListener) as we may select objects at various depths
		const SelectionContext context( panel, mouseX, mouseY, GetDefaultSelectionDepth(), SelectionMode::Subtract, m_marqueeSelectsThroughObjects || GetSelectionHandler().IsMultilayerSelectionEnabled() );
		panel.AttachInputHandler( red::CreateUniquePtr< SelectionScreenAreaInputHandler >( m_target, mouseX, mouseY, context ) );

		return true;
	}

	Bool SelectionInputListener::OnClearSelectionAction()
	{
		// We need to make this a new batch or else we risk merging our clear selection with another Action_Selection and thus Undo will appear functioning incorrectly
		m_target.NextSelectionBatch();
		m_target.ClearSelection();

		return true;
	}

	Bool SelectionInputListener::OnHideSelectionAction()
	{
		m_target.HideSelection();

		return true;
	}

	Bool SelectionInputListener::OnKeyDown( PreviewPanel& panel, const InputKeyEvent& evt )
	{
		// Ugly hack
		// When WSAD keys are pressed ignore selection. This will prevent
		// selecting objects when holding a selection combination and still move camera around (WSAD keys).

		// TODO: this code should be removed. Camera should have own shortcut bindings.
		if ( evt.m_key == EInputKey::IK_W || evt.m_key == EInputKey::IK_S || evt.m_key == EInputKey::IK_A || evt.m_key == EInputKey::IK_D )
		{
			m_ignoreSelection = true;
		}
		return false; // input is transparent
	}

	Bool SelectionInputListener::OnMouseDown( PreviewPanel& panel, const InputMouseButton& evt )
	{
		if ( evt.m_mask )
		{
			m_ignoreSelection = false;
		}

		return false; // input is transparent
	}
} // tools
