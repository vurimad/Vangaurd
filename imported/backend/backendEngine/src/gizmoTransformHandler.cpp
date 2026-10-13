/**
* Copyright (c) 2015-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gizmoTransformHandler.h"
#include "selectionIterator.h"
#include "editActionHistory.h"
#include "snapManager.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( Action_SetTransform, tools )
	RTTI_PARENT_TYPE( tools::IEditAction );
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( Action_SetPivotTransform, tools )
	RTTI_PARENT_TYPE( tools::IEditAction );
RTTI_END_TYPE()

using red::DynArray;

namespace tools
{

	GizmoTransformHandler::GizmoTransformHandler( IEditActionHistory& actions, SnapManager* snapManager, SelectionStatePtr selection, GizmoSpaceType spaceType )
		: m_actionHistory( actions )
		, m_spaceType( spaceType )
		, m_snapManager( snapManager )
		, m_selection( selection )
		, m_movementFailed( false )
		, m_objects{ red::PoolBackend() }
		, m_tempPivotOffset( Transform::IDENTITY() )
		, m_pivotOffset( Transform::IDENTITY() )
	{
		RED_FATAL_ASSERT( m_selection, "Invalid selection pointer" );

		// capture initial transform of all of the objects
		// we can have some kind of filtering here
		for ( auto it = selection->Iterate(); it; ++it )
		{
			auto proxy = it.GetCurrentPtr();
			if ( proxy )
			{
				Info info;
				info.m_proxy = proxy;
				info.m_initialTransform = proxy->GetWorldTransform();
				info.m_initialPivotTransform = proxy->GetPivotTransform();
				info.m_initialScale = proxy->GetScale();
				info.m_canOverrideGizmoType = proxy->CanOverrideGizmoType();
				info.m_gizmoType = proxy->GetGizmoType();
				m_objects.PushBack( info );

				proxy->CacheTransformationPivot( ( info.m_initialPivotTransform * info.m_initialTransform ).GetPosition3() );
			}
		}

		SetupSnapping();
	}

	Transform GizmoTransformHandler::GetObjectPivotSpace(const EditableProxyPtr& ptr) const
	{
		return ptr->GetPivotTransform() * ptr->GetWorldTransform();
	}

	void GizmoTransformHandler::OnReset()
	{
		m_snapManager->SelectionChanged( nullptr );
	}

	Transform GizmoTransformHandler::GetSelectionGizmo() const
	{
		if ( !m_objects.Empty() )
		{
			const Transform localSpaceTransform = GetObjectPivotSpace( m_objects.Back().m_proxy );

			GizmoSpaceType spaceOverride;
			if ( m_spaceType == GizmoSpaceType::World && (!m_objects.Back().m_proxy->GetSpaceOverride( spaceOverride ) || spaceOverride == GizmoSpaceType::World ) )
			{
				return Transform( localSpaceTransform.GetPosition3() );
			}
			else
			{
				return localSpaceTransform;
			}
		}

		return Transform::IDENTITY();
	}

	Transform GizmoTransformHandler::GetGizmoPlacement() const
	{
		return m_tempPivotOffset * ( m_pivotOffset * GetSelectionGizmo() );
	}

	Bool GizmoTransformHandler::CanOverrideGizmoType() const
	{
		if ( !m_objects.Empty() )
		{
			return m_objects.Back().m_canOverrideGizmoType;
		}

		return false;
	}

	GizmoType GizmoTransformHandler::GetGizmoType() const
	{
		if ( !m_objects.Empty() )
		{
			return m_objects.Back().m_gizmoType;
		}

		return GizmoType::Translation;
	}

	void GizmoTransformHandler::OnCaptureTransforms()
	{
		for ( Info& it : m_objects )
		{
			it.m_initialTransform = it.m_proxy->GetWorldTransform();
			it.m_initialPivotTransform = it.m_proxy->GetPivotTransform();
			it.m_initialScale = it.m_proxy->GetScale();
			it.m_proxy->CacheTransformationPivot( ( it.m_initialPivotTransform * it.m_initialTransform ).GetPosition3() );
		}

		SetupSnapping();
	}

	void GizmoTransformHandler::SetupSnapping()
	{
		if ( m_snapManager )
			m_snapManager->SelectionChanged( m_selection );
	}

	void GizmoTransformHandler::OnRevertTransforms()
	{
		for ( const auto& it : m_objects )
		{
			it.m_proxy->SetWorldTransform( it.m_initialTransform );
			it.m_proxy->SetPivotTransform( it.m_initialPivotTransform );
		}

		m_tempPivotOffset = Transform::IDENTITY();
	}

	//---------


	Action_SetTransform::Action_SetTransform()
	{}

	String Action_SetTransform::GetDescription() const
	{
		return "Change transform";
	}

	Bool Action_SetTransform::Do()
{
		// restore ALL objects, if this fails probably something unloaded on the way
		{
			for ( auto& it : m_movements )
				if ( !it.m_obj->RestoreObject() )
					return false;

			for ( auto& it : m_scalings )
				if ( !it.m_obj->RestoreObject() )
					return false;
		}

		for ( auto& it : m_movements )
		{
			it.m_obj->CacheTransformationPivot( ( it.m_obj->GetPivotTransform() * it.m_oldTransform ).GetPosition3() );
		}

		// set new transforms
		for ( auto& it : m_movements )
			it.m_obj->SetWorldTransform( it.m_newTransform );

		// set new scales
		for ( auto& it : m_scalings )
			it.m_obj->SetScale( it.m_newScale );

		// action applied
		return true;
	}

	Bool Action_SetTransform::Undo()
{
		// restore ALL objects, if this fails probably something unloaded on the way
		{
			for ( auto& it : m_movements )
				if ( !it.m_obj->RestoreObject() )
					return false;

			for ( auto& it : m_scalings )
				if ( !it.m_obj->RestoreObject() )
					return false;
		}

		for ( auto& it : m_movements )
		{
			it.m_obj->CacheTransformationPivot( ( it.m_obj->GetPivotTransform() * it.m_newTransform ).GetPosition3() );
		}

		// set old transforms
		for ( auto& it : m_movements )
			it.m_obj->SetWorldTransform( it.m_oldTransform );

		// set old scales
		for ( auto& it : m_scalings )
			it.m_obj->SetScale( it.m_oldScale );

		// action applied
		return true;
	}

	Bool Action_SetTransform::TryMerge( const IEditAction* lastUndoAction )
	{
		if ( lastUndoAction && lastUndoAction->GetClass() == GetStaticClass() )
		{
			const auto* lastTransform = static_cast<const Action_SetTransform*>( lastUndoAction );

			if ( m_movements.Size() != lastTransform->m_movements.Size() )
			{
				return false;
			}

			if ( m_scalings.Size() != lastTransform->m_scalings.Size() )
			{
				return false;
			}

			for ( Uint32 i = 0; i < m_movements.Size(); ++i )
			{
				if ( m_movements[i].m_obj != lastTransform->m_movements[i].m_obj ||
					m_movements[i].m_oldTransform != lastTransform->m_movements[i].m_oldTransform )
				{
					return false;
				}
			}

			for ( Uint32 i = 0; i < m_scalings.Size(); ++i )
			{
				if ( m_scalings[i].m_obj != lastTransform->m_scalings[i].m_obj ||
					m_scalings[i].m_oldScale != lastTransform->m_scalings[i].m_oldScale )
				{
					return false;
				}
			}

			return true;
		}

		return false;
	}

	Bool Action_SetTransform::Preview()
	{
		for ( auto& it : m_movements )
		{
			if ( !it.m_obj->SetWorldTransform( it.m_newTransform, true ) )
			{
				RevertPreview();
				return false;
			}
		}

		for ( auto& it : m_scalings )
		{
			if ( !it.m_obj->SetScale( it.m_newScale ) )
			{
				RevertPreview();
				return false;
			}
		}

		return true;
	}

	void Action_SetTransform::RevertPreview()
	{
		for ( auto& it : m_movements )
		{
			if ( !it.m_obj->SetWorldTransform( it.m_oldTransform, true ) )
			{
				RED_LOG_WARNING( "SetWorldTransform failed for %hs", it.m_obj->GetEditorObjectIDPath().ToString().AsChar() );
				RED_FATAL( "Failed to revert transform preview" );
			}
		}

		for ( auto& it : m_scalings )
		{
			if ( !it.m_obj->SetScale( it.m_oldScale ) )
			{
				RED_LOG_WARNING( "SetScale failed for %hs", it.m_obj->GetEditorObjectIDPath().ToString().AsChar() );
				RED_FATAL( "Failed to revert transform preview" );
			}
		}
	}

	void Action_SetTransform::AddObjectMovement( const EditableProxyPtr& ptr, const Transform& oldTransform, const Transform& newTransform )
	{
		if ( ptr )
		{
			MovementInfo info;
			info.m_obj = ptr;
			info.m_oldTransform = oldTransform;
			info.m_newTransform = newTransform;
			m_movements.PushBack( info );
		}
	}

	void Action_SetTransform::AddObjectScaling( const EditableProxyPtr& ptr, const Vector3& oldScale, const Vector3& newScale )
	{
		if ( ptr )
		{
			ScalingInfo info;
			info.m_obj = ptr;
			info.m_oldScale = oldScale;
			info.m_newScale = newScale;
			m_scalings.PushBack( info );
		}
	}


	Bool Action_SetTransform::IsModification() const
	{
		return true;
	}

	void Action_SetTransform::GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
	{
		for ( const MovementInfo& info : m_movements )
		{
			const EditableProxyPtr& proxy = info.m_obj;
			RED_FATAL_ASSERT( proxy );

			outResourcePaths.Insert( proxy->GetEditorObjectID().GetResourcePath() );
		}

		for ( const ScalingInfo& info : m_scalings )
		{
			const EditableProxyPtr& proxy = info.m_obj;
			RED_FATAL_ASSERT( proxy );

			outResourcePaths.Insert( proxy->GetEditorObjectID().GetResourcePath() );
		}
	}

	//---------


	Action_SetPivotTransform::Action_SetPivotTransform()
	{}

	String Action_SetPivotTransform::GetDescription() const
	{
		return "Change pivot transform";
	}

	Bool Action_SetPivotTransform::Do()
{
		// restore ALL objects, if this fails probably something unloaded on the way
		for ( auto& it : m_transforms )
			if ( !it.m_obj->RestoreObject() )
				return false;

		// set new transforms
		for ( auto& it : m_transforms )
			it.m_obj->SetPivotTransform( it.m_newTransform );

		// action applied
		return true;
	}

	Bool Action_SetPivotTransform::Undo()
{
		// restore ALL objects, if this fails probably something unloaded on the way
		for ( auto& it : m_transforms )
			if ( !it.m_obj->RestoreObject() )
				return false;

		// set old transforms
		for ( auto& it : m_transforms )
			it.m_obj->SetPivotTransform( it.m_oldTransform );

		// action applied
		return true;
	}

	Bool Action_SetPivotTransform::TryMerge( const IEditAction* lastUndoAction )
	{
		if ( lastUndoAction && lastUndoAction->GetClass() == GetStaticClass() )
		{
			const auto* lastTransform = static_cast<const Action_SetPivotTransform*>( lastUndoAction );

			if ( m_transforms.Size() == lastTransform->m_transforms.Size() )
			{
				for ( Uint32 i = 0; i < m_transforms.Size(); ++i )
				{
					if ( m_transforms[i].m_obj != lastTransform->m_transforms[i].m_obj )
						return false;
				}

				for ( Uint32 i = 0; i < m_transforms.Size(); ++i )
				{
					if ( m_transforms[i].m_oldTransform != lastTransform->m_transforms[i].m_oldTransform )
						return false;
				}

				return true;
			}

		}

		return false;
	}

	Bool Action_SetPivotTransform::Preview()
	{
		for ( auto& it : m_transforms )
		{
			if ( !it.m_obj->SetPivotTransform( it.m_newTransform ) )
				return false;
		}
		return true;
	}

	void Action_SetPivotTransform::AddObject( EditableProxyPtr ptr, const Transform& oldTransform, const Transform& newTransform )
	{
		if ( ptr )
		{
			Info info;
			info.m_obj = ptr;
			info.m_oldTransform = oldTransform;
			info.m_newTransform = newTransform;
			m_transforms.PushBack( info );
		}
	}


	Bool Action_SetPivotTransform::IsModification() const
	{
		return true;
	}

	void Action_SetPivotTransform::GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
	{
		for ( const Info& info : m_transforms )
		{
			const EditableProxyPtr& proxy = info.m_obj;
			RED_FATAL_ASSERT( proxy );

			outResourcePaths.Insert( proxy->GetEditorObjectID().GetResourcePath() );
		}
	}

	//---------

	static Vector3 CalcScalingMultiplier( const Vector3& originalDir, const Vector3& scaledDir )
	{
		const Float eps = 1.e-03f;

		return
		{
			fabsf( originalDir.X ) > eps ? scaledDir.X / originalDir.X : 1.0f,
			fabsf( originalDir.Y ) > eps ? scaledDir.Y / originalDir.Y : 1.0f,
			fabsf( originalDir.Z ) > eps ? scaledDir.Z / originalDir.Z : 1.0f
		};
	}

	void GizmoTransformHandler::OnApplyScaling( const Transform& worldSpacePivot, const Vector3& worldDir, Float totalScale, ActionMode actionMode, GizmoTransformMode transformMode, Uint8 axesMask )
	{
		RED_FATAL_ASSERT( transformMode != GizmoTransformMode::ObjectsPivots );

		auto action = red::CreateUniquePtr< Action_SetTransform >();

		const Vector3 worldDirNormalized = worldDir.IsZero() ? Vector3::ONES() : worldDir.Normalized();

		// calculate scaling multiplier in gizmo space
		const Vector3 pivotSpaceDir = worldDir.IsZero() ? Vector3::ONES() : worldSpacePivot.GetOrientation().Inverse() * worldDirNormalized;
		const Vector3 scaledPivotSpaceDir = pivotSpaceDir * totalScale;
		const Vector3 pivotSpaceScaleMultiplier = CalcScalingMultiplier( pivotSpaceDir, scaledPivotSpaceDir );

		for ( const auto& it : m_objects )
		{
			Transform transform = it.m_initialTransform;
		
			// apply transform (if needed)
			if ( transformMode != GizmoTransformMode::ObjectsIndividually )
			{
				// scale positions in gizmo space
				transform = transform * worldSpacePivot.GetInverse();
				transform.SetPosition( transform.GetPosition() * pivotSpaceScaleMultiplier );
				transform = transform * worldSpacePivot;

				action->AddObjectMovement( it.m_proxy, it.m_initialTransform, transform );
			}

			// apply local scaling in object space
			{
				const Vector3 localDir = worldDir.IsZero() ? Vector3::ONES() : transform.GetOrientation().Inverse() * worldDirNormalized;
				const Vector3 scaledLocalDir = localDir * totalScale;
				Vector3 localScaleMultiplier = CalcScalingMultiplier( localDir, scaledLocalDir );

				// apply scale mask
				for ( Uint32 axis = 0; axis < 3; ++axis )
				{
					if ( ( axesMask & RED_FLAG( axis ) ) == 0 )
					{
						localScaleMultiplier[axis] = 1.0f; // Disabled axis (mask = 0) should produce 1.0 scale.
					}
				}

				action->AddObjectScaling( it.m_proxy, it.m_initialScale, it.m_initialScale * localScaleMultiplier );
			}
		}

		ApplyAction( std::move( action ), actionMode );
	}

	void GizmoTransformHandler::OnApplyMovement( const Transform& worldSpacePivot, const Transform& worldSpaceTransform, ActionMode actionMode, GizmoTransformMode transformMode )
	{
		// special cases - move pivot
		if ( transformMode == GizmoTransformMode::ObjectsPivots )
		{
			OnApplyObjectsPivotMovement( worldSpacePivot, worldSpaceTransform, actionMode );
			return;
		}

		auto action = red::CreateUniquePtr< Action_SetTransform >();

		for ( const auto& it : m_objects )
		{
			Transform transform = it.m_initialTransform;

			transform.SetPosition( transform.GetPosition3() - worldSpacePivot.GetPosition3() );
			if ( transformMode == GizmoTransformMode::ObjectsIndividually )
			{
				transform.SetOrientation( worldSpaceTransform.GetOrientation() * transform.GetOrientation() );
			}
			else
			{
				transform = transform * worldSpaceTransform;
			}
			transform.SetPosition( transform.GetPosition3() + worldSpacePivot.GetPosition3() );

			action->AddObjectMovement( it.m_proxy, it.m_initialTransform, transform );
		}	

		ApplyAction( std::move( action ), actionMode );

		if ( actionMode == ActionMode::Final )
		{
			// execute additional snap action if the movement was snapped and the snap solver handles this
			if ( m_cachedPostMovementAction )
			{
				// TODO create group action instead two separate actions?
				m_actionHistory.Execute( std::move( m_cachedPostMovementAction ) );
			}
		}
	}

	void GizmoTransformHandler::OnApplyObjectsPivotMovement( const Transform& worldSpacePivot, const Transform& worldSpaceTransform, const ActionMode actionMode )
	{
		auto action = red::CreateUniquePtr< Action_SetPivotTransform >();

		for ( const auto& it : m_objects )
		{
			Quaternion pivotRotation = it.m_initialPivotTransform.GetOrientation();
			pivotRotation = pivotRotation * it.m_proxy->GetWorldTransform().GetOrientation();
			pivotRotation = pivotRotation * worldSpaceTransform.GetOrientation();
			pivotRotation = it.m_proxy->GetWorldTransform().GetOrientation().Conjugate() * pivotRotation;

			auto pivotTranslation = it.m_initialPivotTransform.GetPosition3();
			pivotTranslation = it.m_proxy->GetWorldTransform().TransformPoint( pivotTranslation );
			pivotTranslation += worldSpaceTransform.GetPosition3();
			pivotTranslation = it.m_proxy->GetWorldTransform().GetInverse().TransformPoint( pivotTranslation );

			action->AddObject( it.m_proxy, it.m_initialPivotTransform, Transform( pivotTranslation, pivotRotation ) );
		}

		ApplyAction( std::move( action ), actionMode );
	}

	void GizmoTransformHandler::ApplyAction( red::UniquePtr< Action_SetTransformBase > action, ActionMode actionMode )
	{
		// preview or send to execution
		if ( actionMode == ActionMode::Preview )
		{
			if ( !m_movementFailed )
			{
				m_movementFailed = !action->Preview();
			}
		}
		else if ( actionMode == ActionMode::Final )
		{
			if ( !m_movementFailed )
			{
				m_actionHistory.Execute( std::move( action ) );
			}

			m_movementFailed = false;
			m_snapManager->ResetVisualization();
		}
	}

	Bool GizmoTransformHandler::OnPreProcessMove( const Transform& worldSpacePivot, const Vector3& worldDir, Float totalMovement, Transform& outTransform )
	{
		// snapping
		if ( m_snapManager )
		{
			const TranslationSnapParams snapParams = 
			{
				worldSpacePivot,
				worldDir,
				totalMovement,
			};

			red::UniquePtr< IEditAction > tempAction;
			Transform transform;
			if ( m_snapManager->ProcessTranslationalSnap( snapParams, transform, tempAction ) )
			{
				m_cachedPostMovementAction = std::move( tempAction );
				outTransform = transform;
				return true;
			}
		}

		// TODO: implement sector limits to disallow people moving stuff outside ?

		// no limits, compute target position manually
		m_cachedPostMovementAction.Reset();
		outTransform = Transform( worldDir * totalMovement );
		return true;
	}

	Bool GizmoTransformHandler::OnPreProcessRotation( const Transform& worldSpacePivot, const Vector3& worldDir, Float totalRotation, Float& outRotation )
	{
		// snap the rotation
		if (m_snapManager)
		{
			const RotationSnapParams snapParams = 
			{
				worldSpacePivot,
				worldDir,
				totalRotation,
			};

			red::UniquePtr< IEditAction > tempAction;
			Float snappedRotation = 0.0f;
			if ( m_snapManager->ProcessRotationSnap( snapParams, snappedRotation, tempAction ) )
			{
				m_cachedPostMovementAction = std::move( tempAction );
				outRotation = snappedRotation;
				return true;
			}
		}

		m_cachedPostMovementAction.Reset();
		outRotation = totalRotation;
		return true;
	}

	Bool GizmoTransformHandler::OnPreProcessScale( const Transform& worldSpacePivot, const Vector3& worldDir, Float totalScale, Float& outScale )
	{
		Bool snapped = false;

		// snap
		if (m_snapManager)
		{
			const ScalingSnapParams snapParams =
			{
				worldSpacePivot,
				worldDir,
				totalScale,
			};

			red::UniquePtr< IEditAction > tempAction;
			Float snappedScale = 0.0f;
			if ( m_snapManager->ProcessScalingSnap( snapParams, snappedScale, tempAction ) )
			{
				m_cachedPostMovementAction = std::move( tempAction );
				outScale = snappedScale;
				snapped = true;
			}
		}

		if ( !snapped )
		{
			outScale = totalScale;
			m_cachedPostMovementAction.Reset();
		}

		// clamp the scale
		outScale = math::Max( 0.001f, outScale );
		outScale = math::Min( 1000.0f, outScale );

		return true;
	}

} // tools
