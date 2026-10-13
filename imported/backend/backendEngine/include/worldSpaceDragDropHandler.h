/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../backendEngine/include/previewDragDropHandler.h"

namespace rend
{
	class Color10Result;
	class DepthResult;
}

namespace tools
{
	struct EditModeViewport;
	class SnapManager;

	/// Drag&Drop handler that is able to place objects in the world space
	/// TODO: this is just the beginning :)
	class BACKEND_ENGINE_API IWorldSpaceDragDropHandler : public IPreviewDragDropHandler
	{
	public:
		IWorldSpaceDragDropHandler( red::SharedPtr< rend::DepthResult >& depthMap, red::SharedPtr< rend::Color10Result > normalMap, SnapManager* snapManager );
		virtual ~IWorldSpaceDragDropHandler();

		/// There's a new position at which user wants to place something
		virtual void OnMouseMoved( const Vector3& position, const Quaternion& rotation ) = 0;

		/// Finalize the drop
		virtual Bool OnFinished( const Vector3& position, const Quaternion& rotation ) = 0;

		/// Render the preview of the placement
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override;

	private:
		virtual void OnMouseMoved( const InputMouseMove& evt ) override final;
		virtual Bool OnFinished( const InputMouseButton& evt ) override final;

		Int32			m_lastMouseX;
		Int32			m_lastMouseY;

		Vector3			m_computedPosition;
		Quaternion		m_computedRotation;
		Bool			m_computedTransformValid;

		red::SharedPtr< rend::DepthResult > m_depthMap;
		red::SharedPtr< rend::Color10Result > m_normalMap;
		SnapManager* m_snapManager;

		// recompute target world space placement
		void RecomputeTransform( const EditModeViewport& vpt, const Int32 x, const Int32 y );
		
		// solve 3D geometrical intersection
		Bool SolveIntersection( const EditModeViewport& vpt, const Int32 pixelX, const Int32 pixelY, Vector3& outPosition, Vector3& outNormal ) const;
	};

} // tools