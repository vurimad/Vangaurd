/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "snapSolver.h"

#include "../../../common/world/include/snapCloudCollection.h"
#include "../../../common/world/include/snapSolver.h"
#include "../../../common/interop/include/interopUtilsManual.h"

namespace world
{
	class RuntimeSystemSnapSovler;
}

namespace tools
{

	/// snap solver implementation that works using the snap features (the other solver is the grid)
	class BACKEND_ENGINE_API SnapSolverSnapFeatures : public ISnapSolver
	{
		DECLARE_INTEROP(SnapSolverSnapFeatures);

	public:
		SnapSolverSnapFeatures(world::RuntimeSystemSnapSovler* solver);

		/// ISnapSolver
		virtual void OnSelectionChanged( const SelectionStatePtr& selection ) override final;
		virtual void OnSourceObjectsChanged( const red::DynArray<EditorObjectID>& ids ) override final;
		virtual Bool OnProcessTranslationalSnap( const TranslationSnapParams& params, const Float limit, Transform& outSnappedTransform, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction ) override final;
		virtual Bool OnProcessRotationSnap( const RotationSnapParams& params, const Float limit, Float& outSnappedRotation, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction ) override final;
		virtual Bool OnProcessScalingSnap( const ScalingSnapParams& params, const Float limit, Float& outSnappedScale, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction ) override final;
		virtual void OnRender(rend::IDebugDrawer& dd) override;
		virtual void OnResetVisualization() override final;
		virtual ISnapSolver::SnapParameters GetSnapParameters() const override;

	private:
		world::RuntimeSystemSnapSovler*	m_solver;
		world::SnapSolverPtr m_activeSolver;

		SelectionStatePtr m_selection;

		world::SnapSolverSetup m_setup;

		Bool m_isEnabled;
	};

} // tools