#include "build.h"
#include "quickPutObjectHandler.h"
#include "selectionHandler.h"
#include "previewInput.h"
#include "previewEditMode.h"
#include "previewPanelSharedData.h"

#include "../../common/renderData/include/renderSelectionResult.h"

namespace tools
{

	QuickPutObjectHandler::QuickPutObjectHandler( red::WeakPtr< IHelperObject > helperObject, const ObjectCreationInfo& info, const ObjectPreviewPtr& objectPreview, red::SharedPtr< rend::DepthResult > depthMap, Bool cloningMode )
		: m_helperObject( helperObject )
		, m_creationInfo( info )
		, m_objectPreview( objectPreview )
		, m_depthMap( std::move( depthMap ) )
		, m_isPreviewValid( false )
		, m_cloningMode( cloningMode )
	{
		RED_FATAL_ASSERT( m_objectPreview, "Object preview expected" );
	}

	QuickPutObjectHandler::~QuickPutObjectHandler()
	{
		m_objectPreview->OnDestroy();
	}

	Bool QuickPutObjectHandler::OnMouseDown( const InputMouseButton& evt )
	{
		if ( evt.m_button == LMB )
		{
			 auto helperObject = m_helperObject.Lock();

			 if ( !helperObject )
				 return false;

			if ( !m_isPreviewValid )
			{
				return true;
			}

			ObjectHandler& objectHandler = helperObject->GetObjectHandler();

			if ( m_cloningMode )
			{
				if ( !objectHandler.CreateObject( m_creationInfo, true ) )
				{
					return true;
				}
			}
			else
			{
				const SelectionStatePtr currentSelection = objectHandler.GetSelectionHandler()->GetSelection();
				if ( !helperObject->GetPreviewPanelSharedData()->MoveSelection( currentSelection, m_creationInfo.m_transform ) )
				{
					return true;
				}
			}

			// update preview if the object was created successfully
			m_creationInfo = ObjectCreationInfo( "point" );
			IHelperObject::QuickPutObjectMode mode;
			helperObject->QueryQuickPutObjectInfo( mode, m_creationInfo );

			if ( mode == IHelperObject::QuickPutObjectMode::None )
			{
				return false;
			}

			m_objectPreview = helperObject->CreateObjectPreview( m_creationInfo );
			RED_FATAL_ASSERT( m_objectPreview, "Object preview expected" );

			// mouse move will be required
			m_isPreviewValid = false;
		}

		return true;
	}

	Bool QuickPutObjectHandler::CalculateObjectPlacement( const EditModeViewport& vpt, Int32 x, Int32 y, Vector3& outWorldPosition ) const
	{
		if ( m_depthMap && m_depthMap->CalcWorldSpacePoint( x, y, outWorldPosition ) )
		{
			return true;
		}

		auto helperObject = m_helperObject.Lock();

		if ( !helperObject )
			return false;

		// compute ray for geometry checks
		Vector3 rayOrigin, rayDirection;
		vpt.CalcRay( x, y, rayOrigin, rayDirection );

		if ( helperObject->CalculateObjectPlacementLocation( rayOrigin, rayDirection, outWorldPosition ) )
		{
			// placement location was determined by the helper object itself
			return true;
		}

		return false;
	}

	Bool QuickPutObjectHandler::OnMouseMoved( const InputMouseMove& evt )
	{
		Vector3 worldSpacePosition;
		m_isPreviewValid = CalculateObjectPlacement( evt.m_viewport, evt.m_x, evt.m_y, worldSpacePosition );

		if ( m_isPreviewValid )
		{
			m_creationInfo.m_transform = Transform( worldSpacePosition );
			m_objectPreview->OnMove( m_creationInfo.m_transform );
		}

		return true;
	}

	Bool QuickPutObjectHandler::OnKeyDown( const InputKeyEvent& evt )
	{
		// If the input handler really captured all the editor input, this would not be needed.
		// However, input handler only captures backend-side input, so for example, it's still possible to call "Undo" action
		// while object handler is present.
		return false;
	}

	Bool QuickPutObjectHandler::OnKeyUp( const InputKeyEvent& evt )
	{
		return false;
	}

	void QuickPutObjectHandler::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{
		RED_FATAL_ASSERT( m_objectPreview );

		if ( m_isPreviewValid )
		{
			m_objectPreview->OnRender( camera, dd );
		}
	}

}