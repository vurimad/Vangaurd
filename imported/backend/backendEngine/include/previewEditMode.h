/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "backendMemory.h"

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/renderData/include/renderCamera.h"

class CRenderFrameInfo;
class CRenderCameraFrameInfo;

namespace tools
{
    class PreviewPanel;
	class PreviewPanelSharedData;
	typedef red::SharedPtr<PreviewPanelSharedData> PreviewPanelSharedDataPtr;

	class IResourceEditor;

    struct UpdateContext;

	/// Viewport context
	struct BACKEND_ENGINE_API EditModeViewport
	{
		CRenderCamera		m_camera;			// camera at the  viewport
		Uint32				m_viewportWidth;	// width of the viewport
		Uint32				m_viewportHeight;	// height of the viewport

		EditModeViewport();
		EditModeViewport( const CRenderCamera& camera, rend::IDebugDrawer& dd );

		/// calculate WORLD SPACE ray representing for ray tracing
		void CalcRay( const Int32 x, const Int32 y, Vector3& outPosition, Vector3& outDirection ) const;

		/// Calculate  2D position (on the screen) for given world space position of object
		/// returns false for positions behind the camera
		/// the Z of the screen position is valid and represents distance form the camera position along the camera direction vector
		/// screen the X and Y components are in pixels
		Bool WorldToScreen( const Vector3& worldPosition, Vector3& outScreenPosition ) const;

		/// Project many points onto screen
		/// the Z of the screen position is valid and represents distance form the camera position along the camera direction vector
		/// screen position X and Y components are in pixels
		void WorldToScreen( const Vector3* points, Vector3* screenSpacePoints, Uint32 count ) const;

		/// Calculate 3D world space position from the position on the screen
		/// the Z of the screen position is valid and represents distance form the camera position along the camera direction vector
		/// the function is built in a way that always returns valid position
		/// screen the X and Y components are in pixels
		void ScreenToWorld( const Vector3& screenPosition, Vector3& outWorldPosition ) const;

		/// Calculate semi-size preserving scaling factor for something at given position
		Float CalcScreenSpaceScale( const Vector3& centerPoint ) const;
	};

	/// Basic edition mode in preview panel
	/// This is used to abstract an user interactions with the preview panel
	/// NOTE: a preview panel has to explicitly register a preview mode it can use since not all preview modes are compatible with all panels
	/// Only one preview mode is active at any given time and the current preview mode is always destroyed before creating new one
	class BACKEND_ENGINE_API IPreviewEditMode
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		IPreviewEditMode();
		virtual ~IPreviewEditMode();

		/// Get display name, this name is rendered in a frame around the viewport to indicate what edit mode is currently active
		virtual const AnsiChar* GetDisplayName() const;

        /// Get display color, this is the colour of the rendered frame around the viewport to indicate what edit mode is currently active
        virtual Color GetDisplayColor() const;

		/// Initialize, called only once when the edit mode is first instanced, can fail
		/// Note that the edit mode is always used with the preview panel and NOT the resource editor itself
		virtual Bool OnInitialize(const PreviewPanelSharedDataPtr& sharedData);

		/// Called when the edit mode gets activated, can deny activation
		virtual Bool OnActivate( IPreviewEditMode* currentMode );

		/// Called when the edit mode gets deactivated, always called before deleting object
		virtual void OnDeactivate();

		/// Render stuff related to this debug mode
		virtual void OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo );

		/// Update internal stuff related to this debug mode
		virtual void OnUpdate( const UpdateContext& ctx );

        /// register a preview panel using this edit mode
        virtual void NotifyPanelCreated(PreviewPanel* panel);

		/// unregister a preview panel using this edit mode
		virtual void NotifyPanelDestroyed(PreviewPanel* panel);

		/// Interop integration
		virtual void OnSetupInterop( ::interop::utils::Object& op );
	};

} /// tools
