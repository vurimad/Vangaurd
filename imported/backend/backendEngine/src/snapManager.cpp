/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "snapManager.h"
#include "selectionHandler.h"

namespace tools
{
	SnapManager::SnapManager()
		: m_globalSnapEnabled(true)
		, m_translationSnapEnabled(true)
		, m_rotationSnapEnabled(true)
		, m_scalingSnapEnabled(true)
		, m_translationSnapLimit(0.05f)
		, m_rotationSnapLimit(0.5f)
		, m_scaleSnapLimit(0.05f)
		, m_solvers{ red::PoolBackend() }
	{
	}

	SnapManager::~SnapManager()
	{
	}

	void SnapManager::ResetVisualization()
	{
		for (auto* ptr : m_solvers)
			ptr->OnResetVisualization();
	}

	void SnapManager::Render(rend::IDebugDrawer& dd)
	{
		for (auto* ptr : m_solvers)
			ptr->OnRender(dd);
	}

	void SnapManager::SelectionChanged( const SelectionStatePtr& selection )
	{
		for ( auto* ptr : m_solvers )
			ptr->OnSelectionChanged( selection );
	}

	void SnapManager::SourceObjectsChanged( red::DynArray<EditorObjectID>& ids )
	{
		for ( auto* ptr : m_solvers )
			ptr->OnSourceObjectsChanged( ids );
	}

	void SnapManager::AttachSolver(ISnapSolver* solver)
	{
		if (solver)
		{
			RED_ASSERT(!m_solvers.Exist(solver), "Solver already registered!");
			m_solvers.PushBack(solver);
		}
	}

	void SnapManager::DetachSolver(ISnapSolver* solver)
	{
		m_solvers.Remove(solver);
	}

	void SnapManager::EnableSnapping(const Bool isEnabled)
	{
		m_globalSnapEnabled = isEnabled;
	}

	void SnapManager::EnableTranslationSnapping(const Bool isEnabled)
	{
		m_translationSnapEnabled = isEnabled;
	}

	void SnapManager::EnableRotationSnapping(const Bool isEnabled)
	{
		m_rotationSnapEnabled = isEnabled;
	}

	void SnapManager::EnableScaleSnapping(const Bool isEnabled)
	{
		m_scalingSnapEnabled = isEnabled;
	}

	void SnapManager::SetMaximumTranslationSnap(const Float snapLimit)
	{
		m_translationSnapLimit = snapLimit;
	}

	void SnapManager::SetMaximumRotationSnap(const Float snapLimit)
	{
		m_rotationSnapLimit = snapLimit;
	}

	void SnapManager::SetMaximumScaleSnap(const Float snapLimit)
	{
		m_scaleSnapLimit = snapLimit;
	}

	void SnapManager::SetupInterop(SnapManager* ptr, ::interop::utils::Object& op)
	{
		op.RegisterValue("enabled", &ptr->m_globalSnapEnabled);
		op.RegisterValue("translationEnabled", &ptr->m_translationSnapEnabled);
		op.RegisterValue("translationLimit", &ptr->m_translationSnapLimit);
		op.RegisterValue("rotationEnabled", &ptr->m_rotationSnapEnabled);
		op.RegisterValue("rotationLimit", &ptr->m_rotationSnapLimit);
		op.RegisterValue("scalingEnabled", &ptr->m_scalingSnapEnabled);
		op.RegisterValue("scaleLimit", &ptr->m_scaleSnapLimit);
	}

	Bool SnapManager::ProcessTranslationalSnap( const TranslationSnapParams& params, Transform& outSnappedTransform, red::UniquePtr< IEditAction >& outPostEditAction )
	{
		// no snapping
		if (!m_globalSnapEnabled || !m_translationSnapEnabled || m_translationSnapLimit <= 0.0f)
			return false;

		// try all snap solvers
		red::UniquePtr< IEditAction > postEditAction;
		Float currentBestCost = RED_FLT_MAX;
		Transform currentBestTransform = Transform::IDENTITY();
		for ( ISnapSolver* ptr : m_solvers )
		{
			red::UniquePtr< IEditAction > tempAction;
			Transform newSnappedTransfrom = Transform::IDENTITY();
			Float newSolvedCost = RED_FLT_MAX;
			if ( ptr->OnProcessTranslationalSnap( params, m_translationSnapLimit, newSnappedTransfrom, newSolvedCost, tempAction ) )
			{
				if (newSolvedCost < currentBestCost)
				{
					currentBestTransform = newSnappedTransfrom;
					currentBestCost = newSolvedCost;
					postEditAction = std::move( tempAction );
				}
			}
		}

		// any solver snapped the position ?
		if (currentBestCost < RED_FLT_MAX)
		{
			outPostEditAction = std::move( postEditAction );
			outSnappedTransform = currentBestTransform;
			return true;
		}

		// no snapping
		return false;
	}

	Bool SnapManager::ProcessRotationSnap( const RotationSnapParams& params, Float& outSnappedRotation, red::UniquePtr< IEditAction >& outPostEditAction )
	{
		// no snapping
		if (!m_globalSnapEnabled || !m_rotationSnapEnabled || m_rotationSnapLimit <= 0.0f)
			return false;

		// try all snap solvers
		Float currentBestCost = RED_FLT_MAX;
		Float currentBest = 0.0f;
		for (auto* ptr : m_solvers)
		{
			red::UniquePtr< IEditAction > tempAction;
			Float newSolvedCost = RED_FLT_MAX;
			Float newSolvedSnapRotation = 0.0f;
			if ( ptr->OnProcessRotationSnap( params, m_rotationSnapLimit, newSolvedSnapRotation, newSolvedCost, tempAction ) )
			{
				if (newSolvedCost < currentBestCost)
				{
					currentBestCost = newSolvedCost;
					currentBest = newSolvedSnapRotation;
				}
			}
		}

		// any solver snapped the rotation  ?
		if (currentBestCost < RED_FLT_MAX)
		{
			outSnappedRotation = currentBest;
			return true;
		}

		// no snapping
		return false;
	}

	Bool SnapManager::ProcessScalingSnap( const ScalingSnapParams& params, Float& outSnappedScale, red::UniquePtr< IEditAction >& outPostEditAction )
	{
		// no snapping
		if (!m_globalSnapEnabled || !m_scalingSnapEnabled || m_scaleSnapLimit <= 0.0f)
			return false;

		// try all snap solvers
		Float currentBestCost = RED_FLT_MAX;
		Float currentBest = params.totalScale;
		for (auto* ptr : m_solvers)
		{
			red::UniquePtr< IEditAction > tempAction;
			Float newSolvedSnapScale = 0.0f;
			Float newSolvedCost = RED_FLT_MAX;
			if ( ptr->OnProcessScalingSnap( params, m_scaleSnapLimit, newSolvedSnapScale, newSolvedCost, tempAction ) )
			{
				if (newSolvedCost < currentBestCost)
				{
					currentBestCost = newSolvedCost;
					currentBest = newSolvedSnapScale;
				}
			}
		}

		// any solver snapped the rotation  ?
		if (currentBestCost < RED_FLT_MAX)
		{
			outSnappedScale = currentBest;
			return true;
		}

		// no snapping
		return false;
	}

	ISnapSolver::SnapParameters SnapManager::GetSnapParametes( const Uint8 solverIndex )
	{
		ISnapSolver::SnapParameters outStruct = { -1.0f, -1.0f, -1.0f };
		if ( m_solvers.Empty() || solverIndex < 0 || solverIndex >= m_solvers.Size() )
			return outStruct;

		return m_solvers[ solverIndex ]->GetSnapParameters();
	}

} // tools
