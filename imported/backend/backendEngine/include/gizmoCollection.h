/**
* Copyright (c) 2015-2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "gizmoFwd.h"
#include "gizmoTypes.h"
#include "selectionFwd.h"

#include "../../../common/interop/include/interopBase.h"
#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{

	/// helper class to manage typical TRS (translation rotation scale) gizmos
	/// you need to provide only ONE interface here
	/// NOTE: this is NOT highly customizable because it's meant to be generic.
	class BACKEND_ENGINE_API GizmoCollection : public red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( GizmoCollection );

	public:

		// the transform handlers are created for when gizmo is created and are required to deal with given set of object
		typedef std::function< GizmoTransformHandlerPtr( SelectionStatePtr selection, GizmoSpaceType spaceType ) > TTransformHandlerFactory;

		GizmoCollection( GizmoManager* manager, TTransformHandlerFactory transformHandlerFactory );
		virtual ~GizmoCollection();

		/// Go to next gizmo type (T -> R -> S - > T)
		void CycleGizmos();

		/// Go to next space type (Local -> Parent -> World -> Local)
		void CycleSpaces();

		/// Select gizmo by "type" (0-T, 1-R, 2-S)
		Bool SelectGizmo( GizmoType gizmoType );

		/// Select operation space
		void SelectSpace( const GizmoSpaceType spaceType );

		/// Selection changed, reevaluate gizmos
		void SelectionChanged( SelectionStatePtr newState );

		/// Get where the gizmo is currently positioned
		Transform GetGizmoPlacement() const;

		/// Get current gizmo type
		RED_FORCE_INLINE const GizmoType GetGizmoType() const { return m_activeGizmo; }

		/// Get space we are operating in
		RED_FORCE_INLINE const GizmoSpaceType GetSpaceType() const { return m_activeSpace; }

	private:
		SelectionStatePtr							m_selection;
		GizmoType									m_activeGizmo;
		GizmoSpaceType								m_activeSpace;

		GizmoTransformHandlerPtr					m_currTransformHandler;
		red::SharedPtr< GizmoGroupTranslate >		m_gizmoGroupTranslate;
		red::SharedPtr< GizmoGroupRotate >			m_gizmoGroupRotate;
		red::SharedPtr< GizmoGroupScale >			m_gizmoGroupScale;

		GizmoManager*								m_gizmoManager;	// NOT OWNED

		TTransformHandlerFactory					m_transformHandlerFactory;

		interop::InteropNotifier					m_gizmoRefreshedNotifier;
		interop::InteropNotifier					m_spaceRefreshedNotifier;

		Bool StrideOverGizmos( Uint8 step );
		Bool CanSelectGizmo( const GizmoType newGizmo ) const;
		void RefreshGizmo();
	};

} // tools