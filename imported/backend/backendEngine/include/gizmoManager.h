/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "gizmo.h"
#include "previewFwd.h"
#include "previewInputListener.h"

namespace tools
{
	class ShortcutManager;

	/// Gizmo manager owns all gizmos that can be available in the edition mode
	/// It is itself an simple input listener and listens mostly to mouse moves and clicks
	class BACKEND_ENGINE_API GizmoManager : public IPreviewInputListener
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		GizmoManager();
		virtual ~GizmoManager() override;

		/// Register gizmo in the manager
		void Register( IGizmo* gizmo );

		/// Unregister gizmo from the manager
		void Unregister( IGizmo* group );

		void RegisterShortcuts( const red::SharedPtr< ShortcutManager >& manager, PreviewPanel* panel );

		/// IPreviewInputListener interface
		virtual CName GetName() const override;

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override;
		virtual Bool OnMouseDown( PreviewPanel& panel, const InputMouseButton& evt ) override;
		virtual Bool OnMouseMoved( PreviewPanel& panel, const InputMouseMove& evt ) override;
		virtual void OnInputHandlerRemoved( PreviewPanel& panel, IPreviewInputHandler* handler, Bool wasCancelled ) override;

		typedef std::function< void() > TOnClone;
		TOnClone OnClone;

	private:
		Bool OnGizmoDragAction( PreviewPanel* panel, Int32 mouseX, Int32 mouseY );
		Bool OnGizmoDragActionConfirmed( PreviewPanel& panel, Int32 mouseX, Int32 mouseY );
		Bool OnTransformObjectsPivotsAction( PreviewPanel& panel );
		Bool OnTransformObjectsIndividuallyAction( PreviewPanel& panel );
		Bool OnTransformGizmoPivotAction( PreviewPanel& panel );

		/// update active gizmo selection
		void UpdateSelection( PreviewPanel& panel, Int32 x, Int32 y, const EditModeViewport& vpt );

		red::SharedPtr< ShortcutManager > m_shortcutManager;

		/// registered gizmos (NOT OWNED)
		typedef	red::DynArray< IGizmo* >	TGroups;
		TGroups			m_gizmos;

		/// active gizmo
		IGizmo*			m_active;
		Bool			m_isDuringAction;
		Bool			m_stopTransactionOnInputListenerDetach;
	};

} // tools
