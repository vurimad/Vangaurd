/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "objectHandler.h"

#include "objectDragDropListener.h"
#include "worldSpaceDragDropHandler.h"
#include "renderingWindow.h"
#include "previewPanel.h"
#include "previewPanelSharedData.h"
#include "previewInput.h"
#include "previewEditMode.h"

#include "editActionHistory.h"
#include "resourceEditor.h"

#include "../../../common/renderData/include/renderSelectionResult.h"

namespace tools
{
	class ObjectDragDropHandler : public IWorldSpaceDragDropHandler
	{
	public:
		ObjectDragDropHandler( ObjectHandler* handler, const ObjectCreationInfo& info, ObjectPreviewPtr previewObject, red::SharedPtr< rend::DepthResult >& depthMap, red::SharedPtr<rend::Color10Result>& normalMap,  SnapManager* snapManager )
			: IWorldSpaceDragDropHandler( depthMap, normalMap, snapManager )
			, m_handler( handler )
			, m_previewObject( previewObject )
			, m_info( info )
		{}

		virtual ~ObjectDragDropHandler()
		{
			m_previewObject->OnDestroy();
		}

		virtual void OnMouseMoved( const Vector3& position, const Quaternion& rotation )  override final
		{
			// update preview transform
			m_info.m_transform = Transform( position, rotation );

			// move the visualization
			m_previewObject->OnMove( m_info.m_transform );
		}

		virtual Bool OnFinished( const Vector3& position, const Quaternion& rotation ) override final
		{
			// update final transform
			m_info.m_transform = Transform( position, rotation );

			// destroy the visualization before creating final object
			m_previewObject->OnDestroy();

			// create final object
			// this WILL create proper undo step and all other fluff
			return m_handler->CreateObject( m_info );
		}

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override final
		{
			// render custom stuff
			m_previewObject->OnRender( camera, dd );
		}

	private:
		ObjectPreviewPtr	m_previewObject;

		ObjectHandler*		m_handler;
		ObjectCreationInfo	m_info;
	};

	//-----

	ObjectDragDropListener::ObjectDragDropListener( ObjectHandler* handler )
		: m_handler( handler )
	{
	}

	ObjectDragDropListener::~ObjectDragDropListener() = default;

	IPreviewDragDropHandler* ObjectDragDropListener::OnDragEnter( PreviewPanel& panel, const String& data, const InputMouseMove& evt )
	{
		/// the depth map is going to be rendered for the whole viewport
		const auto viewportWidth = evt.m_viewport.m_viewportWidth;
		const auto viewportHeight = evt.m_viewport.m_viewportHeight;

		/// setup initial object creation info
		ObjectCreationInfo initInfo(data);
		String reason;
		if ( !m_handler->CanCreateObject( initInfo, &reason ) )
		{
			RED_REPORT_ERROR( RED_LOCATION( m_handler->GetActionHistory()->GetResourceEditor()->GetPath() ), "Cannot % object on % enter: %", RED_NAME_CONSTEXPR( "Create" ), RED_NAME_CONSTEXPR( "Drag" ), reason );

			return nullptr;
		}

		/// render depth map for the whole viewport
		auto depthMap = red::CreateSharedPtr< rend::DepthResult >( red::PoolBackend() );
		auto normalMap = red::CreateSharedPtr< rend::Color10Result >( red::PoolBackend() );
		panel.RenderDepthMap( Rect::Make( 0, 0, viewportWidth, viewportHeight ), depthMap, normalMap );

		// Either that or we need to add complex show/hide system to the preview object
		{
			Vector3 fixedPosition = initInfo.m_transform.GetPosition3();
			fixedPosition.Z = -1000.0f;
			initInfo.m_transform.SetPosition( fixedPosition );
		}

		/// do we accept the data type ? if so, a preview object will be created
		auto objectPreview = m_handler->CreateObjectPreview( initInfo );
		if ( objectPreview )
		{
			Bool snapToTheSurface = data.EndsWith( ".mi" );

			// create the drag&drop handler that will update the preview object and eventually will place it on the scene (or not)
			IPreviewDragDropHandler* handler = RED_NEW( ObjectDragDropHandler )( m_handler, initInfo, objectPreview, std::move( depthMap ), snapToTheSurface ? std::move( normalMap ) : nullptr, &panel.GetSharedData()->GetSnapManager() );

			// HACK: setup fake event to update the handler and preferred world position
			// this will refresh the placement of the object
			handler->OnMouseMoved( evt );
			return handler;
		}

		// drag&drop not serviced
		RED_REPORT_ERROR( RED_LOCATION( m_handler->GetActionHistory()->GetResourceEditor()->GetPath() ), "% & drop failed", RED_NAME_CONSTEXPR( "Drag" ) );

		return nullptr;
	}

	void ObjectDragDropListener::OnDragLeave()
	{
	}

	void ObjectDragDropListener::OnDragDrop()
	{
	}

} // tools
