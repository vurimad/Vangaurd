/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "snapSolverSnapFeatures.h"
#include "selectionIterator.h"
#include "selectionState.h"

#include "../../common/world/include/runtimeSystemSnapping.h"
#include "../../common/world/include/snapCloudCollection.h"
#include "../../common/world/include/snapSolver.h"
#include "../../common/world/include/worldNodeGroup.h"

namespace tools
{

	SnapSolverSnapFeatures::SnapSolverSnapFeatures(world::RuntimeSystemSnapSovler* solver)
		: m_solver(solver)
		, m_isEnabled(true)
	{}

	void SnapSolverSnapFeatures::OnSelectionChanged( const SelectionStatePtr& selection )
	{
		m_selection = selection;
		m_activeSolver.Reset();

		if (m_selection && !m_selection->IsEmpty())
		{
			red::DynArray < tools::EditorObjectID > ids;
			for ( auto it = m_selection->Iterate(); it; ++it )
			{
				if ( THandle< world::NodesGroup > group = Cast< world::NodesGroup >( it->GetSerializable() ) )
				{
					for ( const THandle< world::Node >& node : group->GetAllNodes() )
					{
						ids.PushBack( node->GetEditorObjectID() );
					}
				}
				else
				{
					ids.PushBack( it->GetEditorObjectID() );
				}
			}

			m_activeSolver = m_solver->GetCollection().CreateSolver(ids, m_setup);
		}
	}

	void SnapSolverSnapFeatures::OnSourceObjectsChanged( const red::DynArray<EditorObjectID>& ids )
	{
		m_selection.Reset();
		m_activeSolver.Reset();

		if ( !ids.Empty() )
		{
			m_activeSolver = m_solver->GetCollection().CreateSolver(ids, m_setup);
		}
	}

	Bool SnapSolverSnapFeatures::OnProcessTranslationalSnap( const TranslationSnapParams& params, const Float limit, Transform& outSnappedTransform, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction )
	{
		if ( m_activeSolver && m_isEnabled )
		{
			Vector3 snappedTranslation = Vector3::ZEROS();
			const Vector3 linearMovement = params.worldDir * params.totalMovement;
			if ( m_activeSolver->SolveLinearSnappingConstraint( params.worldPivot.GetPosition3(), linearMovement, snappedTranslation ) )
			{
				outSnappedTransform = Transform( snappedTranslation );
				outCost = 0.5f;
				return true;
			}
		}

		return false;
	}

	Bool SnapSolverSnapFeatures::OnProcessRotationSnap( const RotationSnapParams& params, const Float limit, Float& outSnappedRotation, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction )
	{
		return false;
	}

	Bool SnapSolverSnapFeatures::OnProcessScalingSnap( const ScalingSnapParams& params, const Float limit, Float& outSnappedScale, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction )
	{
		return false;
	}
	
	void SnapSolverSnapFeatures::OnRender(rend::IDebugDrawer& dd)
	{
		if (m_activeSolver && m_isEnabled)
			m_activeSolver->Render(dd);
	}

	void SnapSolverSnapFeatures::OnResetVisualization()
	{
		m_activeSolver.Reset();
	}

	ISnapSolver::SnapParameters SnapSolverSnapFeatures::GetSnapParameters() const
	{
		ISnapSolver::SnapParameters outStruct = { -1.0f, -1.0f, -1.0f };
		return outStruct;
	}

	void SnapSolverSnapFeatures::SetupInterop( SnapSolverSnapFeatures* ptr, ::interop::utils::Object& op )
	{
		op.RegisterValue<Bool>("enabled",
			[ptr](::interop::IContext& ctx, Bool& outValue) { outValue = ptr->m_isEnabled; },
			[ptr](::interop::IContext& ctx, const Bool& newValue) { ptr->m_isEnabled = newValue; });

		op.RegisterValue<Float>("maxLinearLimit",
			[ptr](::interop::IContext& ctx, Float& outValue) { outValue = ptr->m_setup.m_maxLinearLimit; },
			[ptr](::interop::IContext& ctx, const Float& newValue) { ptr->m_setup.m_maxLinearLimit = newValue; });

		op.RegisterValue<Float>("maxAngularLimit",
			[ptr](::interop::IContext& ctx, Float& outValue) { outValue = ptr->m_setup.m_maxAngularLimit; },
			[ptr](::interop::IContext& ctx, const Float& newValue) { ptr->m_setup.m_maxAngularLimit = newValue; });

		op.RegisterValue<Bool>("allowMoveX",
			[ptr](::interop::IContext& ctx, Bool& outValue) { outValue = ptr->m_setup.m_allowMoveX; },
			[ptr](::interop::IContext& ctx, const Bool& newValue) { ptr->m_setup.m_allowMoveX = newValue; });

		op.RegisterValue<Bool>("allowMoveY",
			[ptr](::interop::IContext& ctx, Bool& outValue) { outValue = ptr->m_setup.m_allowMoveY; },
			[ptr](::interop::IContext& ctx, const Bool& newValue) { ptr->m_setup.m_allowMoveY = newValue; });

		op.RegisterValue<Bool>("allowMoveZ",
			[ptr](::interop::IContext& ctx, Bool& outValue) { outValue = ptr->m_setup.m_allowMoveZ; },
			[ptr](::interop::IContext& ctx, const Bool& newValue) { ptr->m_setup.m_allowMoveZ = newValue; });

		op.RegisterValue<Bool>("allowRotation",
			[ptr](::interop::IContext& ctx, Bool& outValue) { outValue = ptr->m_setup.m_allowRotation; },
			[ptr](::interop::IContext& ctx, const Bool& newValue) { ptr->m_setup.m_allowRotation = newValue; });

		op.RegisterValue<Uint32>("sourceDepth",
			[ptr](::interop::IContext& ctx, Uint32& outValue) { outValue = ptr->m_setup.m_sourceDepth; },
			[ptr](::interop::IContext& ctx, const Uint32& newValue) { ptr->m_setup.m_sourceDepth = newValue; });

		op.RegisterValue<Uint32>( "targetDepth",
			[ptr]( ::interop::IContext& ctx, Uint32& outValue ) { outValue = ptr->m_setup.m_targetDepth; },
			[ptr]( ::interop::IContext& ctx, const Uint32& newValue ) { ptr->m_setup.m_targetDepth = newValue; } );
	}

} // tools