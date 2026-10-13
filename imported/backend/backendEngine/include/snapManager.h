/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "snapSolver.h"

#include "../../../common/interop/include/interopBase.h"
#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{

	/// generalized snapping control utility
	/// all snapping ALWAYS occurs in world space (for simplicity)
	/// NOTE: this will get numerically unstable at some point once the world gets big enough and will require rewrite, but not now ;] 
	class BACKEND_ENGINE_API SnapManager
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( SnapManager );

	public:
		SnapManager();
		virtual ~SnapManager();

		/// Reset all visualization for the snapping, called after the snapping is no longer needed
		void ResetVisualization();

		/// Render snap visualization
		void Render(rend::IDebugDrawer& dd);

		/// Pointer is still owned by you
		void AttachSolver( ISnapSolver* solver );

		/// Detach snapping solver
		void DetachSolver( ISnapSolver* solver );

		/// Global enable/disable
		void EnableSnapping( const Bool isEnabled );

		/// Enable translation snapping
		void EnableTranslationSnapping( const Bool isEnabled );

		/// Enable rotation snapping
		void EnableRotationSnapping( const Bool isEnabled );

		/// Enable scale snapping
		void EnableScaleSnapping( const Bool isEnabled );

		/// Set maximum translation snap distance
		void SetMaximumTranslationSnap( const Float snapLimit );

		/// Set maximum rotation snap distance (angles)
		void SetMaximumRotationSnap( const Float snapLimit );

		/// Set maximum scale snap distance
		void SetMaximumScaleSnap( const Float snapLimit );

		/// Notify snap manager about new selection
		/// Some snapping features require access to active selection
		void SelectionChanged( const SelectionStatePtr& selection );
		void SourceObjectsChanged( red::DynArray< EditorObjectID >& ids );

		/// Process translational snapping in world space
		/// Returns true if snapping occurs and returns the new position
		/// Due to usability common sense it's legal for the translational snapping to move point 
		/// The movement is passed as base.translation + dir*offset since more information can be infered from this representation that just from raw position, also it's more numerically stable
		/// This runs all attached snapping solvers and selects the snap that is the best (closest snap)
		Bool ProcessTranslationalSnap( const TranslationSnapParams& params, Transform& outSnappedTransform, red::UniquePtr< IEditAction >& outPostEditAction );

		/// Process rotation snapping in world space
		/// Returns true if snapping occurs and returns the new rotation ANGLE
		/// It's not planned for the snapping to change the rotation angle
		/// This runs all attached snapping solvers and selects the snap that is the best (closest snap)
		Bool ProcessRotationSnap( const RotationSnapParams& params, Float& outSnappedRotation, red::UniquePtr< IEditAction >& outPostEditAction );

		/// Process scale snapping in world space
		/// Returns true if snapping occurs and returns the new scaling FACTOR
		/// The scaling "axis" represents the relative direction along which the object is scaled
		/// It's not planned for the snapping to change the form of scaling
		/// This runs all attached snapping solvers and selects the snap that is the best (closest snap)
		Bool ProcessScalingSnap( const ScalingSnapParams& params, Float& outSnappedScale, red::UniquePtr< IEditAction >& outPostEditAction );

		ISnapSolver::SnapParameters GetSnapParametes( const Uint8 solverIndex );

	private:
		Bool m_globalSnapEnabled;

		Bool m_translationSnapEnabled;
		Bool m_rotationSnapEnabled;
		Bool m_scalingSnapEnabled;

		Float m_translationSnapLimit;
		Float m_rotationSnapLimit;
		Float m_scaleSnapLimit;

		red::DynArray< ISnapSolver* > m_solvers;
	}; 
	
} // tools
