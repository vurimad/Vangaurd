#pragma once

#include "previewFwd.h"
#include "previewInputHandler.h"
#include "objectHandler.h"
#include "helperObject.h"

namespace tools
{
	class WorldPreviewPanelSharedData;

	/// Input handler that allows for quick creation of object withing a "helper object" (e.g. spline points)
	/// The handler is activated when special key is pressed (Ctrl is default) and is destroyed when the key is released.
	class BACKEND_ENGINE_API QuickPutObjectHandler : public IPreviewInputHandler
	{
	public:
		QuickPutObjectHandler( red::WeakPtr< IHelperObject > helperObject, const ObjectCreationInfo& info, const ObjectPreviewPtr& previewObject, red::SharedPtr< rend::DepthResult > depthMap, Bool cloningMode );
		~QuickPutObjectHandler();

		virtual Bool OnMouseDown( const InputMouseButton& evt ) override;
		virtual Bool OnMouseMoved( const InputMouseMove& evt ) override;
		virtual Bool OnKeyDown( const InputKeyEvent& evt ) override;
		virtual Bool OnKeyUp( const InputKeyEvent& evt ) override;

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override;

	private:
		red::WeakPtr< IHelperObject > m_helperObject;
		ObjectCreationInfo m_creationInfo;
		ObjectPreviewPtr m_objectPreview;
		red::SharedPtr< rend::DepthResult > m_depthMap; // optional
		Bool m_cloningMode;
		
		// If 'false', don't draw the preview and don't put an object on LMB
		Bool m_isPreviewValid;

		Bool CalculateObjectPlacement( const EditModeViewport& vpt, Int32 x, Int32 y, Vector3& outWorldPosition ) const;
	};
}
