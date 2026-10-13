/**
* Copyright (c) 2015-2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "simpleGridSnapper.h"
#include "selectionHandler.h"

namespace tools
{

	SimpleGridSnapper::SimpleGridSnapper()
		: m_snapMode( ESnapMode::Gizmo )
		, m_positionGrid( 0.1f )
		, m_rotationGrid( 15.0f )
		, m_scalingGrid( 0.1f )
		, m_positionSnapEnabled( true )
		, m_rotationSnapEnabled( true )
		, m_scalingSnapEnabled( true )
	{
	}

	void SimpleGridSnapper::OnSelectionChanged( const SelectionStatePtr& selection )
	{
		RED_UNUSED( selection );
	}

	void SimpleGridSnapper::OnSourceObjectsChanged( const red::DynArray<EditorObjectID>& ids )
	{
		RED_UNUSED( ids );
	}

	namespace helper
	{
		// TODO: this is numerically inaccurate when grid size has no finite expansion in the floating point representation
		RED_FORCE_INLINE const Float Snap(const Float x, const Float gridSize)
		{
			return ( Float )( round( ( red::Double )x / ( red::Double )gridSize ) * ( red::Double )gridSize );
		}
	}

	Bool SimpleGridSnapper::OnProcessTranslationalSnap( const TranslationSnapParams& params, const Float limit, Transform& outSnappedTransform, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction )
	{
		if ( !m_positionSnapEnabled )
		{
			// no snapping
			return false;
		}

		if (m_snapMode == ESnapMode::Absolute)
		{
			const Vector3 worldBase = params.worldPivot.GetPosition();
			Vector3 targetPos = worldBase + params.worldDir * params.totalMovement;

			targetPos.X = helper::Snap(targetPos.X, m_positionGrid);
			targetPos.Y = helper::Snap(targetPos.Y, m_positionGrid);
			targetPos.Z = helper::Snap(targetPos.Z, m_positionGrid);

			outSnappedTransform = Transform( targetPos - worldBase ); // NOTE: beware of precision issues
			outCost = 1.0f; // high cost
		}
		else if (m_snapMode == ESnapMode::Gizmo)
		{
			const Vector3 movement = params.worldDir * params.totalMovement;

			const Vector3 axisX = params.worldPivot.GetOrientation().GetXAxis3();
			const Vector3 axisY = params.worldPivot.GetOrientation().GetYAxis3();
			const Vector3 axisZ = params.worldPivot.GetOrientation().GetZAxis3();

			// decompose movement into local axes
			Float movementX = axisX.Dot( movement );
			Float movementY = axisY.Dot( movement );
			Float movementZ = axisZ.Dot( movement );

			movementX = helper::Snap( movementX, m_positionGrid );
			movementY = helper::Snap( movementY, m_positionGrid );
			movementZ = helper::Snap( movementZ, m_positionGrid );

			outSnappedTransform = Transform( axisX * movementX + axisY * movementY + axisZ * movementZ );
			outCost = 1.0f; // high cost
		}

		return true;
	}

	Bool SimpleGridSnapper::OnProcessRotationSnap( const RotationSnapParams& params, const Float limit, Float& outSnappedRotation, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction )
	{
		if ( m_rotationSnapEnabled )
		{
			if ( m_snapMode == ESnapMode::Absolute )
			{
				// TODO: solve rotation angle such as that when applied to rotational part of captured placement matrix will generate "snappable" angles

				const Float snappedMovement = helper::Snap( params.totalRotation, m_rotationGrid );
				outSnappedRotation = snappedMovement;
				outCost = 1.0f; // high cost

				return true;
			}
			else if ( m_snapMode == ESnapMode::Gizmo )
			{
				const Float snappedMovement = helper::Snap( params.totalRotation, m_rotationGrid );
				outSnappedRotation = snappedMovement;
				outCost = 1.0f; // high cost

				return true;
			}
		}

		// no snapping
		return false;
	}

	Bool SimpleGridSnapper::OnProcessScalingSnap( const ScalingSnapParams& params, const Float limit, Float& outSnappedScale, Float& outCost, red::UniquePtr< IEditAction >& outPostEditAction )
	{
		if ( m_scalingSnapEnabled )
		{
			// no difference between relative and absolute snapping when dealing with scales
			const Float snappedScale = helper::Snap( params.totalScale, m_scalingGrid );
			outSnappedScale = snappedScale;
			outCost = 1.0f; // high cost

			return true;
		}

		// no snapping
		return false;
	}

	ISnapSolver::SnapParameters SimpleGridSnapper::GetSnapParameters() const
	{
		ISnapSolver::SnapParameters outStruct = { m_positionGrid, m_rotationGrid, m_scalingGrid };
		return outStruct;
	}

	void SimpleGridSnapper::OnRender( rend::IDebugDrawer& dd )
	{
		// nothing
	}

	void SimpleGridSnapper::OnResetVisualization()
	{
		// nothing
	}

	void SimpleGridSnapper::SetMode( ESnapMode mode )
	{
		m_snapMode = mode;
	}

	void SimpleGridSnapper::SetPositionGrid( const Float gridSize )
	{
		if ( gridSize > 0.0f )
			m_positionGrid = gridSize;
	}

	void SimpleGridSnapper::SetRotationGrid( const Float gridSize )
	{
		if ( gridSize > 0.0f )
			m_rotationGrid = gridSize;
	}

	void SimpleGridSnapper::SetScalingGrid( const Float gridSize )
	{
		if ( gridSize > 0.0f )
			m_scalingGrid = gridSize;
	}

	void SimpleGridSnapper::EnablePositionalSnapping( const Bool isEnabled )
	{
		m_positionSnapEnabled = isEnabled;
	}

	void SimpleGridSnapper::EnableRotationalSnapping( const Bool isEnabled )
	{
		m_rotationSnapEnabled = isEnabled;
	}

	void SimpleGridSnapper::EnableScalingSnapping( const Bool isEnabled )
	{
		m_scalingSnapEnabled = isEnabled;
	}

	void SimpleGridSnapper::SetupInterop( SimpleGridSnapper* ptr, ::interop::utils::Object& op )
	{
		op.RegisterValue<String>( "mode",
			/*GET*/ [ptr](::interop::IContext& ctx, String& outValue) { outValue = ptr->m_snapMode == ESnapMode::Absolute ? "Abs" : "Gizmo"; },
			/*SET*/ [ptr](::interop::IContext& ctx, const String& newValue) { ptr->SetMode( newValue == "Abs" ? ESnapMode::Absolute : ESnapMode::Gizmo ); } );

		op.RegisterValue<Bool>( "positionSnapEnabled", 
			/*GET*/ [ptr](::interop::IContext& ctx, Bool& outValue) { outValue = ptr->m_positionSnapEnabled; },
			/*SET*/ [ptr](::interop::IContext& ctx, const Bool& newValue) { ptr->EnablePositionalSnapping( newValue ); } );

		op.RegisterValue<Bool>( "rotationSnapEnabled", 
			/*GET*/ [ptr](::interop::IContext& ctx, Bool& outValue) { outValue = ptr->m_rotationSnapEnabled; },
			/*SET*/ [ptr](::interop::IContext& ctx, const Bool& newValue) { ptr->EnableRotationalSnapping( newValue ); } );

		op.RegisterValue<Bool>( "scalingSnapEnabled", 
			/*GET*/ [ptr](::interop::IContext& ctx, Bool& outValue) { outValue = ptr->m_scalingSnapEnabled; },
			/*SET*/ [ptr](::interop::IContext& ctx, const Bool& newValue) { ptr->EnableScalingSnapping( newValue ); } );

		op.RegisterValue<Float>( "positionGrid", 
			/*GET*/ [ptr](::interop::IContext& ctx, Float& outValue) { outValue = ptr->m_positionGrid; },
			/*SET*/ [ptr](::interop::IContext& ctx, const Float& newValue) { ptr->SetPositionGrid( newValue ); } );

		op.RegisterValue<Float>( "rotationGrid", 
			/*GET*/ [ptr](::interop::IContext& ctx, Float& outValue) { outValue = ptr->m_rotationGrid; },
			/*SET*/ [ptr](::interop::IContext& ctx, const Float& newValue) { ptr->SetRotationGrid( newValue ); } );

		op.RegisterValue<Float>( "scalingGrid", 
			/*GET*/ [ptr](::interop::IContext& ctx, Float& outValue) { outValue = ptr->m_scalingGrid; },
			/*SET*/ [ptr](::interop::IContext& ctx, const Float& newValue) { ptr->SetScalingGrid( newValue ); } );
	}

} // tools