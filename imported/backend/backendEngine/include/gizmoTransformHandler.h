/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "gizmoFwd.h"
#include "editableFwd.h"
#include "selectionState.h"

#include "gizmoTypes.h"
#include "editAction.h"

namespace tools
{

	class SnapManager;
	class ISnapSolver;

	class BACKEND_ENGINE_API Action_SetTransformBase : public IEditAction
	{
	public:
		virtual Bool Preview() = 0;
	};

	class BACKEND_ENGINE_API Action_SetTransform : public Action_SetTransformBase
	{
		RTTI_DECLARE_TYPE( Action_SetTransform );

	public:
		Action_SetTransform();

		virtual String GetDescription() const override;
		virtual Bool Do() override;
		virtual Bool Undo() override;
		virtual Bool TryMerge( const IEditAction* lastUndoAction ) override;
		virtual Bool IsModification() const override final;
		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final;
		virtual Bool Preview() override final;

		void AddObjectMovement( const EditableProxyPtr& ptr, const Transform& oldTransform, const Transform& newTransform );
		void AddObjectScaling( const EditableProxyPtr& ptr, const Vector3& oldScale, const Vector3& newScale );

	private:
		void UpdateParentResources();
		void RevertPreview();

		struct MovementInfo
		{
			EditableProxyPtr m_obj;
			Transform m_oldTransform;
			Transform m_newTransform;
		};

		struct ScalingInfo
		{
			EditableProxyPtr m_obj;
			Vector3 m_oldScale;
			Vector3 m_newScale;
		};

		red::DynArray< MovementInfo > m_movements;
		red::DynArray< ScalingInfo > m_scalings;
	};

	class BACKEND_ENGINE_API Action_SetPivotTransform : public Action_SetTransformBase
	{
		RTTI_DECLARE_TYPE( Action_SetPivotTransform );

	public:
		Action_SetPivotTransform();

		virtual String GetDescription() const override;
		virtual Bool Do() override;
		virtual Bool Undo() override;
		virtual Bool TryMerge( const IEditAction* lastUndoAction ) override;
		virtual Bool IsModification() const override final;
		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final;
		virtual Bool Preview() override final;

		void AddObject( EditableProxyPtr ptr, const Transform& oldTransform, const Transform& newTransform );

	private:
		struct Info
		{
			EditableProxyPtr		m_obj;
			Transform				m_oldTransform;
			Transform				m_newTransform;
		};

		red::DynArray< Info > m_transforms;
	};

	/// default (works using action system) handler for gizmo transformation
	class BACKEND_ENGINE_API GizmoTransformHandler
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		enum class ActionMode
		{
			Preview,
			Final,
		};

		GizmoTransformHandler( IEditActionHistory& actions, SnapManager* snapManager, SelectionStatePtr selection, GizmoSpaceType spaceType );

		// get world-space gizmo placement (this is selection gizmo refined with temporary gizmo offset)
		Transform GetGizmoPlacement() const;

		Bool CanOverrideGizmoType() const;
		GizmoType GetGizmoType() const;

		void OnReset();
		void OnCaptureTransforms();
		void OnRevertTransforms();

		void OnApplyMovement( const Transform& worldSpacePivot, const Transform& worldSpaceTransform, ActionMode actionMode, GizmoTransformMode transformMode );
		void OnApplyScaling( const Transform& worldSpacePivot, const Vector3& worldDir, Float totalScale, ActionMode actionMode, GizmoTransformMode transformMode, Uint8 axesMask = 0xFF );

		Bool OnPreProcessMove( const Transform& worldSpacePivot, const Vector3& worldDir, Float totalMovement, Transform& outTargetTransform );
		Bool OnPreProcessRotation( const Transform& worldSpacePivot, const Vector3& worldDir, Float totalRotation, Float& outRotation );
		Bool OnPreProcessScale( const Transform& worldSpacePivot, const Vector3& worldDir, Float totalScale, Float& outScale );

	private:
		IEditActionHistory& m_actionHistory;	// undo/redo system
		SnapManager* m_snapManager;				// NOT OWNED, may be null
		SelectionStatePtr m_selection;			// full, original selection

		struct Info
		{
			EditableProxyPtr m_proxy;			// targeted proxy
			Transform m_initialTransform;		// transform at the start of the operation
			Transform m_initialPivotTransform;
			Vector3 m_initialScale;
			Bool m_canOverrideGizmoType;
			GizmoType m_gizmoType;
		};

		Transform m_tempPivotOffset;
		Transform m_pivotOffset;

		red::DynArray< Info > m_objects; // object we operate on

		// additional action that has to be performed after object movement
		red::UniquePtr< IEditAction > m_cachedPostMovementAction;

		GizmoSpaceType m_spaceType;
		Bool m_movementFailed;

		// get base gizmo of the selection (depends on the selected objects only)
		Transform GetSelectionGizmo() const;

		void OnApplyObjectsPivotMovement( const Transform& worldSpacePivot, const Transform& worldSpaceTransform, ActionMode actionMode );

		void ApplyAction( red::UniquePtr< Action_SetTransformBase > action, ActionMode actionMode );
		void SetupSnapping();

		Transform GetObjectPivotSpace( const EditableProxyPtr& ptr ) const;
	};

} // tools
