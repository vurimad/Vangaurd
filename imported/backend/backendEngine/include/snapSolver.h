/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "undoFwd.h"
#include "selectionFwd.h"
#include "backendMemory.h"

namespace tools
{

	class IEditAction;

	struct TranslationSnapParams
	{
		Transform worldPivot;
		Vector3 worldDir;
		Float totalMovement;
	};

	struct RotationSnapParams
	{
		Transform worldPivot;
		Vector3 worldAxis;
		Float totalRotation;
	};

	struct ScalingSnapParams
	{
		Transform worldPivot;
		Vector3 worldAxis;
		Float totalScale;
	};

	// Snapping solver interface
	// There can be many solvers used by the snapping manager (that snap different object in different ways).
	class BACKEND_ENGINE_API ISnapSolver
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~ISnapSolver() {}

		struct SnapParameters
		{
			Float TranslationParameter;
			Float RotationParameter;
			Float ScaleParameter;
		};

		/// Notify snap manager about new selection
		/// Some snapping features require access to active selection
		virtual void OnSelectionChanged( const SelectionStatePtr& selection ) = 0;

		/// Notify snap manager about new snapping source objects - use in case you want to override selection snapping with preview objects snapping
		virtual void OnSourceObjectsChanged( const red::DynArray< EditorObjectID >& ids ) = 0;

		/// Process translational snapping in world space
		/// Returns true if snapping occurs and returns the new position
		/// Due to usability common sense it's legal for the translational snapping to move point 
		/// The movement is passed as worldPivot.translation + dir*offset since more information can be infered from this representation that just from raw position, also it's more numerically stable
		/// Optionally returns an edit action that has to be applied after movement for the snapping to take effect.
		virtual Bool OnProcessTranslationalSnap( const TranslationSnapParams& params, const Float limit, Transform& outSnappedTransform, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction ) = 0;

		/// Process rotation snapping in world space
		/// Returns true if snapping occurs and returns the new rotation ANGLE
		/// It's not planned for the snapping to change the rotation angle
		/// Optionally returns an edit action that has to be applied after movement for the snapping to take effect.
		virtual Bool OnProcessRotationSnap( const RotationSnapParams& params, const Float limit, Float& outSnappedRotation, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction ) = 0;

		/// Process scale snapping in world space
		/// Returns true if snapping occurs and returns the new scaling FACTOR
		/// The scaling "axis" represents the relative direction along which the object is scaled
		/// It's not planned for the snapping to change the form of scaling
		/// Optionally returns an edit action that has to be applied after movement for the snapping to take effect.
		virtual Bool OnProcessScalingSnap( const ScalingSnapParams& params, const Float limit, Float& outSnappedScale, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction ) = 0;

		/// Reset all visualization for the snapping, called after the snapping is no longer needed
		virtual void OnResetVisualization() = 0;

		/// Render snap visualization
		virtual void OnRender( rend::IDebugDrawer& dd ) = 0;

		/// Get struct of current values in snapping toolbar 
		virtual SnapParameters GetSnapParameters() const = 0;
	};
	
} // tools
