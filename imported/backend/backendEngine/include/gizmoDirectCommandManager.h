/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "gizmoFwd.h"
#include "gizmoCollection.h"
#include "simpleGridSnapper.h"

namespace tools
{
	class BACKEND_ENGINE_API GizmoDirectManager
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		GizmoDirectManager( GizmoCollection::TTransformHandlerFactory factory, SimpleGridSnapper* snapper );
		virtual ~GizmoDirectManager();

		void SelectionChanged( SelectionStatePtr newState );
		void SelectSpace( GizmoSpaceType newSpace ) { m_activeSpace = newSpace; }

		Bool DirectRotate( const Float angle ) const;
		Bool DirectScale( const Float increment ) const;

		void RegisterShortcuts( class ShortcutManager* shortcutManager );

	private:
		Float GetAngle() const;
		Float GetScale() const;

		GizmoSpaceType m_activeSpace;
		GizmoCollection::TTransformHandlerFactory m_gizmoFactory;
		SelectionStatePtr m_selection;
		SimpleGridSnapper* m_simpleGridSnapper;
	};

} // tools
