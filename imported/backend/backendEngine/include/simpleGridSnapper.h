/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "snapSolver.h"

#include "../../../common/interop/include/interopBase.h"
#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{
	/// Simple grid snapping to ABSOLUTE WORLD SPACE GRID
	/// Snapping options are editable via interop and usually displayed somewhere in the viewport
	class BACKEND_ENGINE_API SimpleGridSnapper : public ISnapSolver
	{
		DECLARE_INTEROP( SimpleGridSnapper );

	public:
		SimpleGridSnapper();

		enum class ESnapMode : Uint8
		{
			Absolute,		// we try to snap to absolute world points, this is best for aligning stuff
			Gizmo,			// we snap with respect to our starting gizmo position/rotation (delta snapping)
		};
	
		// ISnapSolver interface - selection changed
		virtual void OnSelectionChanged( const SelectionStatePtr& selection ) override;
		virtual void OnSourceObjectsChanged( const red::DynArray< EditorObjectID >& ids ) override;

		// ISnapSolver interface - snapping solvers
		virtual Bool OnProcessTranslationalSnap( const TranslationSnapParams& params, const Float limit, Transform& outSnappedTransform, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction ) override;
		virtual Bool OnProcessRotationSnap( const RotationSnapParams& params, const Float limit, Float& outSnappedRotation, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction ) override;
		virtual Bool OnProcessScalingSnap( const ScalingSnapParams& params, const Float limit, Float& outSnappedScale, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction ) override;

		// ISnapSolver interface - snapping parameters
		virtual ISnapSolver::SnapParameters GetSnapParameters() const override;

		// ISnapSolver interface - rendering
		virtual void OnRender(rend::IDebugDrawer& dd) override final;
		virtual void OnResetVisualization() override final;

		// set snapping mode
		void SetMode( ESnapMode mode );

		// set positional grid scale
		void SetPositionGrid( const Float gridSize );

		// set rotational grid size
		void SetRotationGrid( const Float gridSize );

		// set scale grid size
		void SetScalingGrid( const Float gridSize );

		// enable/disable positional snapping
		void EnablePositionalSnapping( const Bool isEnabled );

		// enable/disable rotational snapping
		void EnableRotationalSnapping( const Bool isEnabled );

		// enable/disable scale snapping
		void EnableScalingSnapping( const Bool isEnabled );

	private:
		Float		m_positionGrid;		// position grid (used both for translation and scale)
		Float		m_rotationGrid;		// rotation grid
		Float		m_scalingGrid;		// scale grid

		Bool		m_positionSnapEnabled;	// we can snap positions (translation/scale)
		Bool		m_rotationSnapEnabled;  // we can snap rotations
		Bool		m_scalingSnapEnabled;	// we can snap scales

		ESnapMode	m_snapMode;			// snapping mode (absolute, relative)
	};

} // tools