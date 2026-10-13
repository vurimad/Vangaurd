/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gizmoDirectCommandManager.h"
#include "gizmoTransformHandler.h"
#include "selectionIterator.h"
#include "../../common/generalEditorBackend/include/generalEditorBackendPublic.h"

namespace tools
{
	GizmoDirectManager::GizmoDirectManager( GizmoCollection::TTransformHandlerFactory factory, SimpleGridSnapper* snapper )
		: m_gizmoFactory( factory )
		, m_simpleGridSnapper( snapper )
	{
	}

	GizmoDirectManager::~GizmoDirectManager() = default;

	void GizmoDirectManager::SelectionChanged( SelectionStatePtr newState )
	{
		if ( newState )
		{
			for ( auto it = newState->Iterate(); it; ++it )
			{
				if ( !it->SupportsGizmoTransformation() )
				{
					m_selection.Reset();
					return;
				}
			}
		}
		m_selection = newState;
	}

	Bool GizmoDirectManager::DirectScale( const Float increment ) const
	{
		if ( m_selection && !m_selection->IsEmpty() && !m_selection->IsReadOnly() )
		{
			world::NodeScalingMode currentScalingMode = world::NodeScalingMode::All;
			for ( auto it = m_selection->Iterate(); it; ++it )
			{
				Uint8 scalingModeInt = Min( static_cast< Uint8 >( currentScalingMode ), static_cast< Uint8 >( it->GetScalingMode() ) );
				currentScalingMode = static_cast< world::NodeScalingMode >( scalingModeInt );
			}

			if ( currentScalingMode != world::NodeScalingMode::None )
			{
				auto handler = m_gizmoFactory( m_selection, m_activeSpace );
				if ( handler )
				{
					const auto placement = handler->GetGizmoPlacement();
					const auto translation = placement.GetPosition3();
					const Vector3 worldDir = placement.TransformVector( Vector3::ONES() ).Normalized(); // uniform scaling
					
					// Currently the gizmo handles only multiplicative scaling
					Float adjustedIncrement = increment;
					if ( increment < -1.0f )
						adjustedIncrement = 1.0f / increment;
					
					adjustedIncrement = Clamp( adjustedIncrement, -0.9f, 1.0f );

					handler->OnApplyScaling( translation, worldDir, 1.0f + adjustedIncrement, GizmoTransformHandler::ActionMode::Final, GizmoTransformMode::Objects );

					return true;
				}
			}
		}

		return false;
	}

	Bool GizmoDirectManager::DirectRotate( const Float angle ) const
	{
		if ( m_selection && !m_selection->IsEmpty() && !m_selection->IsReadOnly() )
		{
			auto handler = m_gizmoFactory( m_selection, m_activeSpace );
			if ( handler )
			{
				const Vector3 zAxis( 0.0f, 0.0f, 1.0f );
				const auto placement = handler->GetGizmoPlacement();
				const Vector3 worldDir = placement.TransformVector( zAxis ).Normalized();

				const Transform transform( Quaternion( worldDir, DEG2RAD( angle ) ) );
				handler->OnApplyMovement( placement, transform, GizmoTransformHandler::ActionMode::Final, GizmoTransformMode::Objects );

				return true;
			}
		}

		return false;
	}

	Float GizmoDirectManager::GetAngle() const
	{
		Float angle = 15.0f; // default
		if ( m_simpleGridSnapper )
		{
			angle = m_simpleGridSnapper->GetSnapParameters().RotationParameter;
		}
		return angle;
	}

	Float GizmoDirectManager::GetScale() const
	{
		Float scale = 0.1f; // default
		if ( m_simpleGridSnapper )
		{
			scale = m_simpleGridSnapper->GetSnapParameters().ScaleParameter;
		}
		return scale;
	}

	void GizmoDirectManager::RegisterShortcuts( ShortcutManager* manager )
	{
		// Rotate Anticlockwise
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_LeftBracket );

			ShortcutCallback callback = [ this ]( const KeyEventData& )
			{
				return DirectRotate( GetAngle() );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idKBRotatePosSmall" ).Binding( binding ).Callback( callback ).Name( "Rotate CCW" ).Description( "Rotation angle is taken from snapping toolbar" );

			manager->Register( shortcut );
		}

		// Rotate Clockwise
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_RightBracket );

			ShortcutCallback callback = [ this ]( const KeyEventData& )
			{
				return DirectRotate( -GetAngle() );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idKBRotateNegSmall" ).Binding( binding ).Callback( callback ).Name( "Rotate CW" ).Description( "Rotation angle is taken from snapping toolbar" );

			manager->Register( shortcut );
		}

		// Rotate Anticlockwise Large
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_LeftBracket ).Modifier( EInputKey::IK_LShift );

			ShortcutCallback callback = [ this ]( const KeyEventData& )
			{
				return DirectRotate( 90.0f );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idKBRotatePosLarge" ).Binding( binding ).Callback( callback ).Name( "Rotate 90 degrees CCW" ).Description( "Fixed 90 degree rotation" );

			manager->Register( shortcut );
		}

		// Rotate Clockwise Large
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_RightBracket ).Modifier( EInputKey::IK_LShift );

			ShortcutCallback callback = [ this ]( const KeyEventData& )
			{
				return DirectRotate( -90.0f );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idKBRotateNegLarge" ).Binding( binding ).Callback( callback ).Name( "Rotate 90 degrees CW" ).Description( "Fixed 90 degree rotation" );

			manager->Register( shortcut );
		}

		// Scale Up
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_Equals );

			ShortcutCallback callback = [ this ]( const KeyEventData& )
			{
				return DirectScale( GetScale() );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idScalePos" ).Binding( binding ).Callback( callback ).Name( "Scale up" ).Description( "Scale factor is taken from snapping toolbar" );

			manager->Register( shortcut );
		}

		// Scale Down
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_Minus );

			ShortcutCallback callback = [ this ]( const KeyEventData& )
			{
				return DirectScale( -GetScale() );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idScaleNeg" ).Binding( binding ).Callback( callback ).Name( "Scale down" ).Description( "Scale factor is taken from snapping toolbar" );

			manager->Register( shortcut );
		}
	}
}
