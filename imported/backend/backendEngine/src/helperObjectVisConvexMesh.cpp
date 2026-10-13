/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "helperObjectVisConvexMesh.h"
#include "editAction.h"
#include "editActionHistory.h"
#include "selectionHandler.h"
#include "previewInputHandler.h"
#include "../../common/core/include/editableProxy.h"
#include "../../common/input/include/inputKeys.h"
#include "../../common/renderData/include/renderSelectionResult.h"
#include "../../backendWorld/include/worldEditorState_HelperObjects.h"

// TEMPORARY
#if 0

namespace tools
{
	namespace prv
	{
		const EInputKey enterAddObjectModeKey = IK_LControl;
		const EInputKey enterAddFaceModeKey = IK_LShift;
	}


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	class Action_VisConvexMeshAddVertex : public IEditAction
	{
	public:
		Action_VisConvexMeshAddVertex( const String desc, SelectionHandler* selectionHandler, const Vector3& pos, Uint32 faceIndex, Uint16 vertIndex )
			: m_desc( desc )
			, m_selectionHandler( selectionHandler )
			, m_position( pos )
			, m_faceIndex( faceIndex )
			, m_vertIndex( vertIndex )
		{
			// capture current selection so it can be restored
			m_capturedOldSelection = m_selectionHandler->GetSelection()->Clone();
		}

		virtual CName GetInternalName() const override
		{
			return ACTION_NAME;
		}

		virtual String GetDescription() const override
		{
			return m_desc;
		}

		virtual Bool Do( const IEditActionHistory& actionHistory ) override
		{
			// Deselect everything before applying changes
			{
				SelectionStatePtr newSelection( new SelectionState() );
				m_selectionHandler->Action_ChangeSelection( newSelection );
			}

			auto node = Cast<world::StaticOccluderConvexMeshNode>(GetTarget(actionHistory)->GetNode());
			RED_ASSERT(node != nullptr, "Expected convex occluder node");
			node->AddVertexAt( m_faceIndex, m_vertIndex, m_position );

			// Select new vertex
			{
				SelectionStatePtr newSelection( new SelectionState() );
				HelperObjectVisConvexMeshVertexPtr vertexObj = GetTarget(actionHistory)->GetVertexSubobject( m_faceIndex, m_vertIndex );
				newSelection->Add( vertexObj->Selectable_CreateProxy() );
				m_selectionHandler->Action_ChangeSelection( newSelection );
			}

			return true;
		}

		virtual Bool Undo( const IEditActionHistory& actionHistory ) override
		{
			// Deselect everything
			{
				SelectionStatePtr newSelection( new SelectionState() );
				m_selectionHandler->Action_ChangeSelection( newSelection );
			}

			auto node = Cast<world::StaticOccluderConvexMeshNode>(GetTarget(actionHistory)->GetNode());
			RED_ASSERT(node != nullptr, "Expected convex occluder node");
			node->RemoveVertex( m_faceIndex, m_vertIndex );

			// Restore original selection
			m_selectionHandler->Action_ChangeSelection( m_capturedOldSelection );

			return true;
		}

		virtual Bool TryMerge( const IEditAction* lastUndoAction ) override
		{
			return false;
		}

	private:
		HelperObjectVisConvexMesh* GetTarget(const IEditActionHistory& actionHistory)
		{
			auto* ptr = actionHistory.GetContext(CName("helpers")).GetActionContextData(WorldEditorState_HelperObjects::DATA_ID_HELPER_OBJECT);
			RED_FATAL_ASSERT(ptr != nullptr, "Area undo action run outside the convex occluder edition context");

			return (HelperObjectVisConvexMesh*)ptr;
		}


		static CName ACTION_NAME;

		String								m_desc;
		red::SharedPtr< SelectionState >	m_capturedOldSelection;
		SelectionHandler*					m_selectionHandler;
		Vector3								m_position;
		Uint32								m_faceIndex;
		Uint16								m_vertIndex;
	};

	CName Action_VisConvexMeshAddVertex::ACTION_NAME( "VIS_CONVEX_MESH_ADD_VERTEX" );


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	class Action_VisConvexMeshDeleteVertex : public IEditAction
	{
	public:
		struct Info
		{
			enum DeleteFaceTag { DELETE_FACE };

			Vector3 m_previousPosition;			// Position before deletion (for undo)

			Uint32 m_faceIndex;					// Face it was deleted from
			Uint16 m_vertIndex;					// Index within face

			Bool m_deleteFace;					// Is it face deletion (instead of vertex)

			// Delete the given vertex.
			Info( const Vector& prevPos, Uint32 faceIndex, Uint32 vertIndex )
				: m_previousPosition( prevPos )
				, m_faceIndex( faceIndex )
				, m_vertIndex( vertIndex )
				, m_deleteFace( false )
			{}

			// Delete the given face. Assumes that all vertices in the face have already been deleted.
			Info( DeleteFaceTag, Uint32 faceIndex )
				: m_faceIndex( faceIndex )
				, m_deleteFace( true )
			{}
		};

		Action_VisConvexMeshDeleteVertex( const String desc, SelectionHandler* selectionHandler, const red::DynArray< Info >& deleteInfo )
			: m_desc( desc )
			, m_selectionHandler( selectionHandler )
			, m_info( deleteInfo )
		{
			m_capturedOldSelection = m_selectionHandler->GetSelection()->Clone();
		}

		virtual CName GetInternalName() const override
		{
			return ACTION_NAME;
		}

		virtual String GetDescription() const override
		{
			return m_desc;
		}

		virtual Bool Do( const IEditActionHistory& actionHistory ) override
		{
			// Deselect everything before applying changes
			{
				SelectionStatePtr newSelection( new SelectionState() );
				m_selectionHandler->Action_ChangeSelection( newSelection );
			}

			auto node = Cast<world::StaticOccluderConvexMeshNode>(GetTarget(actionHistory)->GetNode());
			RED_ASSERT(node != nullptr, "Expected convex occluder node");

			for ( const Info& info : m_info )
			{
				if ( info.m_deleteFace )
				{
					node->RemoveFace( info.m_faceIndex );
				}
				else
				{
					node->RemoveVertex( info.m_faceIndex, info.m_vertIndex );
				}
			}

			return true;
		}

		virtual Bool Undo( const IEditActionHistory& actionHistory ) override
		{
			// Deselect everything
			{
				SelectionStatePtr newSelection( new SelectionState() );
				m_selectionHandler->Action_ChangeSelection( newSelection );
			}

			auto node = Cast<world::StaticOccluderConvexMeshNode>(GetTarget(actionHistory)->GetNode());
			RED_ASSERT(node != nullptr, "Expected convex occluder node");

			// Undo must be done in reversed order, so the indices aren't messed up
			for ( Uint32 i = m_info.Size(); i-- > 0; )
			{
				if ( m_info[i].m_deleteFace )
				{
					node->AddEmptyFaceAt( m_info[i].m_faceIndex );
				}
				else
				{
					node->AddVertexAt( m_info[i].m_faceIndex, m_info[i].m_vertIndex, m_info[i].m_previousPosition );
				}
			}

			// Restore original selection
			m_selectionHandler->Action_ChangeSelection( m_capturedOldSelection );

			return true;
		}

		virtual Bool TryMerge( const IEditAction* lastUndoAction ) override
		{
			return false;
		}

	private:
		HelperObjectVisConvexMesh* GetTarget(const IEditActionHistory& actionHistory)
		{
			auto* ptr = actionHistory.GetContext(CName("helpers")).GetActionContextData(WorldEditorState_HelperObjects::DATA_ID_HELPER_OBJECT);
			RED_FATAL_ASSERT(ptr != nullptr, "Area undo action run outside the convex occluder edition context");

			return (HelperObjectVisConvexMesh*)ptr;
		}


		static CName ACTION_NAME;

		String								m_desc;
		red::SharedPtr< SelectionState >	m_capturedOldSelection;
		SelectionHandler*					m_selectionHandler;
		red::DynArray< Info >				m_info;
	};

	CName Action_VisConvexMeshDeleteVertex::ACTION_NAME( "VIS_CONVEX_MESH_DELETE_VERTEX" );


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	class Action_VisConvexMeshAddFace : public IEditAction
	{
	public:
		Action_VisConvexMeshAddFace( const String desc, SelectionHandler* selectionHandler, Uint32 faceIndex, const red::DynArray<Vector3>& vertices )
			: m_desc( desc )
			, m_selectionHandler( selectionHandler )
			, m_faceIndex( faceIndex )
			, m_vertices( vertices )
		{
			m_capturedOldSelection = m_selectionHandler->GetSelection()->Clone();
		}

		virtual CName GetInternalName() const override
		{
			return ACTION_NAME;
		}

		virtual String GetDescription() const override
		{
			return m_desc;
		}

		virtual Bool Do( const IEditActionHistory& actionHistory ) override
		{
			// Deselect everything before applying changes
			{
				SelectionStatePtr newSelection( new SelectionState() );
				m_selectionHandler->Action_ChangeSelection( newSelection );
			}

			auto node = Cast<world::StaticOccluderConvexMeshNode>(GetTarget(actionHistory)->GetNode());
			RED_ASSERT(node != nullptr, "Expected convex occluder node");
			node->AddFaceAt( m_faceIndex, m_vertices );

			// Select new face
			{
				SelectionStatePtr newSelection( new SelectionState() );
				HelperObjectVisConvexMeshFacePtr pointHelperObject = GetTarget(actionHistory)->GetFaceSubobject( m_faceIndex );
				newSelection->Add( pointHelperObject->Selectable_CreateProxy() );
				m_selectionHandler->Action_ChangeSelection( newSelection );
			}

			return true;
		}

		virtual Bool Undo( const IEditActionHistory& actionHistory ) override
		{
			// Deselect everything
			{
				SelectionStatePtr newSelection( new SelectionState() );
				m_selectionHandler->Action_ChangeSelection( newSelection );
			}

			auto node = Cast<world::StaticOccluderConvexMeshNode>(GetTarget(actionHistory)->GetNode());
			RED_ASSERT(node != nullptr, "Expected convex occluder node");
			node->RemoveFace( m_faceIndex );

			// Restore original selection
			m_selectionHandler->Action_ChangeSelection( m_capturedOldSelection );

			return true;
		}

		virtual Bool TryMerge( const IEditAction* lastUndoAction ) override
		{
			return false;
		}

	private:
		HelperObjectVisConvexMesh* GetTarget(const IEditActionHistory& actionHistory)
		{
			auto* ptr = actionHistory.GetContext(CName("helpers")).GetActionContextData(WorldEditorState_HelperObjects::DATA_ID_HELPER_OBJECT);
			RED_FATAL_ASSERT(ptr != nullptr, "Area undo action run outside the convex occluder edition context");

			return (HelperObjectVisConvexMesh*)ptr;
		}


		static CName ACTION_NAME;

		String								m_desc;
		red::SharedPtr< SelectionState >	m_capturedOldSelection;
		SelectionHandler*					m_selectionHandler;
		Uint32								m_faceIndex;
		red::DynArray<Vector3>				m_vertices;
	};

	CName Action_VisConvexMeshAddFace::ACTION_NAME( "VIS_CONVEX_MESH_ADD_FACE" );


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	// Input handler for adding vertices and faces.
	class InputHandler_VisConvexMeshAddObject : public IPreviewInputHandler
	{
	private:
		enum class Mode
		{
			None,
			AddVertex,
			AddFace
		};

	public:
		InputHandler_VisConvexMeshAddObject( HelperObjectVisConvexMesh* meshObj, const PreviewPanel& panel, HelperObjectVisConvexMesh::AddVertexPreview* addPointPreview, HelperObjectVisConvexMesh::AddFacePreview* addFacePreview, SelectionHandler* selectionHandler, IEditActionHistory* actionHistory )
			: m_meshObj( meshObj )
			, m_addPointPreview( addPointPreview )
			, m_addFacePreview( addFacePreview )
			, m_selectionHandler( selectionHandler )
			, m_actionHistory( actionHistory )
			, m_currentMode( Mode::AddVertex )
		{
			// Get depth image, so we can place new faces on the scene surfaces
			m_depthMap = red::CreateUniquePtr<rend::DepthResult>();
			panel.RenderDepthMap( Rect::Make( 0, 0, panel.GetWidth(), panel.GetHeight() ), *m_depthMap );
		}

		IPreviewInputHandler* OnKeyDown( const InputKeyEvent& evt ) override
		{
			// If it's the "add face" key, switch modes
			if ( evt.m_key == prv::enterAddFaceModeKey )
			{
				m_currentMode = Mode::AddFace;
				m_addPointPreview->m_enable = false;
				m_addFacePreview->m_enable = true;
				return this;
			}
			
			// Otherwise just end the tool.
			m_depthMap.Reset();
			m_currentMode = Mode::None;
			m_addPointPreview->m_enable = false;
			m_addFacePreview->m_enable = false;
			return nullptr;
		}

		virtual IPreviewInputHandler* OnMouseMoved( const InputMouseMove& evt ) override
		{
			// #TODO: As-is, the mouse needs to move before we can show any preview. Would be good to update the preview
			// when the button is first pressed, but we don't seem to have mouse position at that time.

			if ( m_currentMode == Mode::AddVertex )
			{
				// For AddPoint, find the edge that is closest to the mouse cursor.
				// #TODO: If a face or edge is selected, maybe that should be given priority?

				Vector3 origin, dir;
				evt.m_viewport.CalcRay( evt.m_x, evt.m_y, origin, dir );
				Segment raySegment( origin, dir * 10000.0f );

				float bestDistance = FLT_MAX;
				Uint32 edgeId = 0;
				Uint32 faceId = 0;
				Float edgePos = 0.0f;

				const Matrix localToWorld = m_meshObj->GetTransform();

				const auto& faces = m_meshObj->GetOccluderNode()->GetFaces();
				for ( Uint32 f = 0; f < faces.Size(); ++f )
				{
					const auto& verts = faces[f];
					for ( Uint32 i = 0; i < verts.Size(); ++i )
					{
						const Vector3 pointA = localToWorld.TransformPoint( verts[i] );
						const Vector3 pointB = localToWorld.TransformPoint( verts[( i + 1 ) % verts.Size()] );

						Segment edgeSegment( pointA, pointB - pointA );

						Float rayT, edgeT;
						Float d = math::Segment::SegmentDistance( raySegment, edgeSegment, rayT, edgeT );
						if ( rayT > 0.0f && d < bestDistance )
						{
							bestDistance = d;
							edgeId = i;
							faceId = f;
							edgePos = edgeT;
						}
					}
				}

				if ( bestDistance < FLT_MAX && edgePos > 0.001f && edgePos < 0.999f )
				{
					m_addPointPreview->m_enable = true;
					m_addPointPreview->m_faceIndex = faceId;
					m_addPointPreview->m_index = edgeId;
					m_addPointPreview->m_position = edgePos;
				}
				else
				{
					m_addPointPreview->m_enable = false;
				}
			}
			else if ( m_currentMode == Mode::AddFace )
			{
				// For AddFace, check the rendered depth image and get the world position where the user is pointing.
				// The face will be added at that point, oriented based on the surface normal. This should make it
				// somewhat easier to add occluders for walls and stuff.

				Vector3 point;
				Vector3 normal( 0,0,1 );
				if ( !m_depthMap || !m_depthMap->CalcWorldSpacePoint( evt.m_x, evt.m_y, point ) )
				{
					// If there was no depth image, or the mouse isn't over some rendered surface, try to place it on
					// the Z=0 plane.

					Vector3 rayOrigin, rayDir;
					evt.m_viewport.CalcRay( evt.m_x, evt.m_y, rayOrigin, rayDir );

					Float dist;
					Plane plane( Vector( 0.0f, 0.0f, 1.0f ), 0.0f );
					Vector planePoint;
					Plane::ESide side = plane.IntersectLine( rayOrigin, rayDir, planePoint, dist );
					point = planePoint;

					// If we're looking up from below, flip the normal so the new face will still be facing the camera.
					if ( side == Plane::PS_Back )
					{
						normal = -normal;
					}

					// Intersection point is behind camera, so deactivate the preview.
					if ( dist < 0.0f )
					{
						m_addFacePreview->m_enable = false;
						return this;
					}

					m_addFacePreview->m_enable = true;
				}
				else
				{
					// Got a world position, so try to also get a surface normal approximation.
					// #TODO: This tends to not be giving very good results... maybe take few samples from a bigger
					// neighborhood, and average it all together?
					Vector3 ptx[2], pty[2];
					if (   m_depthMap->CalcWorldSpacePoint(evt.m_x-1, evt.m_y, ptx[0])
						&& m_depthMap->CalcWorldSpacePoint(evt.m_x+1, evt.m_y, ptx[1])
						&& m_depthMap->CalcWorldSpacePoint(evt.m_x, evt.m_y-1, pty[0])
						&& m_depthMap->CalcWorldSpacePoint(evt.m_x, evt.m_y+1, pty[1]))
					{
						normal = (ptx[1]-ptx[0]).Cross(pty[0]-pty[1]);
					}
				}

				const Matrix& worldToLocal = m_meshObj->GetInvertTransform();
				point = worldToLocal.TransformPoint( point );

				const Matrix& worldToLocalTransInv = m_meshObj->GetTransform().Transposed();
				normal = worldToLocalTransInv.TransformVector( normal );

				m_addFacePreview->m_center = point;
				m_addFacePreview->m_normal = normal.Normalized();
			}

			return this;
		}

		virtual IPreviewInputHandler* OnMouseDown( const InputMouseClick& evt ) override
		{
			// Exit the tool on clicking anything other than left button.
			if ( evt.m_button != LMB )
			{
				m_addPointPreview->m_enable = false;
				m_addFacePreview->m_enable = false;
				m_currentMode = Mode::None;
				return nullptr;
			}

			if ( m_currentMode == Mode::AddVertex && m_addPointPreview->m_enable )
			{
				auto node = m_meshObj->GetOccluderNode();
				const auto& verts = node->GetFaceVertices( m_addPointPreview->m_faceIndex );

				const Vector3& pointA = verts[ m_addPointPreview->m_index ];
				const Vector3& pointB = verts[( m_addPointPreview->m_index + 1 ) % verts.Size()];
				const Vector3 newPointPos = math::Lerp( m_addPointPreview->m_position, pointA, pointB );
				const Uint16 newPointIndex = m_addPointPreview->m_index + 1;
				const Uint32 newPointFaceIndex = m_addPointPreview->m_faceIndex;

				m_actionHistory->Execute( new Action_VisConvexMeshAddVertex( "Area shape add point", m_selectionHandler, newPointPos, newPointFaceIndex, newPointIndex ) );
				m_addPointPreview->m_enable = false;
				m_addFacePreview->m_enable = false;
				m_currentMode = Mode::None;
			}
			else if ( m_currentMode == Mode::AddFace && m_addFacePreview->m_enable )
			{
				// Add a face, and exit "add" mode
				auto node = m_meshObj->GetOccluderNode();

				const Uint16 newFaceIndex = node->GetNumFaces();
				const red::DynArray<Vector3> newFaceVertices = world::StaticOccluderConvexMeshNode::CreateDefaultFace( m_addFacePreview->m_center, m_addFacePreview->m_normal );
				m_actionHistory->Execute( new Action_VisConvexMeshAddFace( "Occluder shape add face", m_selectionHandler, newFaceIndex, newFaceVertices ) );

				m_addPointPreview->m_enable = false;
				m_addFacePreview->m_enable = false;
				m_currentMode = Mode::None;
			}

			// NOTE: Theoretically we should now disable input handler (by returning nullptr), but this would make the
			// mouse down event pass to selection handler and some random object would get selected. Instead we still
			// keep the input handler, but because the mode is now None we shouldn't be processing anything at least.
			// Eventually the user will release the Add key, and then we'll end fully.

			return this;
		}

		virtual IPreviewInputHandler* OnKeyUp( const InputKeyEvent& evt ) override
		{
			// Released the "add face" key, so return to AddPoint mode.
			if ( evt.m_key == prv::enterAddFaceModeKey )
			{
				m_currentMode = Mode::AddVertex;
				m_addPointPreview->m_enable = true;
				m_addFacePreview->m_enable = false;
				return this;
			}

			// Released the "add object" key, so we can exit the tool.
			if ( evt.m_key == prv::enterAddObjectModeKey )
			{
				m_depthMap.Reset();
				m_currentMode = Mode::None;
				m_addPointPreview->m_enable = false;
				m_addFacePreview->m_enable = false;
				return nullptr;
			}

			return this;
		}

	private:
		HelperObjectVisConvexMesh*						m_meshObj;

		red::UniquePtr< rend::DepthResult >				m_depthMap;

		HelperObjectVisConvexMesh::AddVertexPreview*	m_addPointPreview;
		HelperObjectVisConvexMesh::AddFacePreview*		m_addFacePreview;
		SelectionHandler*								m_selectionHandler;
		IEditActionHistory*								m_actionHistory;

		Mode											m_currentMode;
	};


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	HelperObjectVisConvexMesh::HelperObjectVisConvexMesh( const world::HelperObjectDesc& desc, EditableProxyPtr editableProxy, IEditActionHistory* actionHistory )
		: IHelperObject( desc, editableProxy )
		, m_actionHistory( actionHistory )
	{
		m_originalFaces = GetOccluderNode()->GetFaces();
		GetOccluderNode()->SetShapeListener(this);

		RefreshSubobjects();
	}

	HelperObjectVisConvexMesh::~HelperObjectVisConvexMesh()
	{
		GetOccluderNode()->SetShapeListener(nullptr);
	}


	void HelperObjectVisConvexMesh::ConvexMeshNode_OnVertexAdded( Uint16 faceIdx, Uint16 vertIdx )
	{
		m_vertices[faceIdx].InsertAt( vertIdx, CreateHandle< HelperObjectVisConvexMeshVertex >( this, faceIdx, vertIdx ) );
		m_edges[faceIdx].InsertAt( vertIdx, CreateHandle< HelperObjectVisConvexMeshEdge >( this, faceIdx, vertIdx ) );

		// correct indices of the remaining vertex/edge helper objects
		for ( Uint32 i = vertIdx + 1; i < m_vertices[faceIdx].Size(); ++i )
		{
			m_vertices[faceIdx][i]->m_vertIndex = i;
			m_edges[faceIdx][i]->m_edgeIndex = i;
		}
	}

	void HelperObjectVisConvexMesh::ConvexMeshNode_OnVertexChanged( Uint16 faceIdx, Uint16 vertIdx )
	{
		m_faces[faceIdx]->RefreshCachedData();
	}

	void HelperObjectVisConvexMesh::ConvexMeshNode_OnVertexRemoved( Uint16 faceIdx, Uint16 vertIdx )
	{
		m_vertices[faceIdx].RemoveAt( vertIdx );
		m_edges[faceIdx].RemoveAt( vertIdx );

		// correct indices of the remaining vertex/edge helper objects
		for ( Uint32 i = vertIdx; i < m_vertices[faceIdx].Size(); ++i )
		{
			m_vertices[faceIdx][i]->m_vertIndex = i;
			m_edges[faceIdx][i]->m_edgeIndex = i;
		}
	}

	void HelperObjectVisConvexMesh::ConvexMeshNode_OnFaceAdded( Uint16 faceIdx )
	{
		m_faces.InsertAt( faceIdx, CreateHandle< HelperObjectVisConvexMeshFace >( this, faceIdx ) );
		m_vertices.InsertAt( faceIdx, TVertexArray() );
		m_edges.InsertAt( faceIdx, TEdgeArray() );

		// add vertices/edges for the new face
		RefreshEdgeVertSubobjects( faceIdx );

		// fix indices of existing helper objects
		for ( Uint32 i = faceIdx + 1; i < m_faces.Size(); ++i )
		{
			m_faces[i]->m_faceIndex = i;
			for ( auto& vert : m_vertices[i] )
			{
				vert->m_faceIndex = i;
			}
			for ( auto& edge : m_edges[i] )
			{
				edge->m_faceIndex = i;
			}
		}
	}

	void HelperObjectVisConvexMesh::ConvexMeshNode_OnFaceRemoved( Uint16 faceIdx )
	{
		m_faces.RemoveAt( faceIdx );
		m_vertices.RemoveAt( faceIdx );
		m_edges.RemoveAt( faceIdx );

		// fix indices of existing helper objects
		for ( Uint32 i = faceIdx; i < m_faces.Size(); ++i )
		{
			m_faces[i]->m_faceIndex = i;
			for ( auto& vert : m_vertices[i] )
			{
				vert->m_faceIndex = i;
			}
			for ( auto& edge : m_edges[i] )
			{
				edge->m_faceIndex = i;
			}
		}
	}

	void HelperObjectVisConvexMesh::ConvexMeshNode_OnShapeChanged()
	{
		RefreshSubobjects();
	}

	void HelperObjectVisConvexMesh::RefreshSubobjects()
	{
		m_faces.Clear();
		m_vertices.Clear();
		m_edges.Clear();

		auto node = GetOccluderNode();

		const Uint32 numFaces =  node->GetNumFaces();
		m_faces.Resize(numFaces);
		m_vertices.Resize(numFaces);
		m_edges.Resize(numFaces);
		for ( Uint32 f = 0; f < numFaces; ++f )
		{
			m_faces[f] = CreateHandle< HelperObjectVisConvexMeshFace >( this, f );
			RefreshEdgeVertSubobjects( f );
		}
	}

	void HelperObjectVisConvexMesh::RefreshEdgeVertSubobjects( Uint16 faceIdx )
	{
		auto node = GetOccluderNode();
		const Uint32 numVerts = node->GetFaceVertices( faceIdx ).Size();

		m_vertices[faceIdx].Resize( numVerts );
		m_edges[faceIdx].Resize( numVerts );
		for ( Uint32 i = 0; i < numVerts; ++i )
		{
			m_vertices[faceIdx][i] = CreateHandle< HelperObjectVisConvexMeshVertex >( this, faceIdx, i );
			m_edges[faceIdx][i] = CreateHandle< HelperObjectVisConvexMeshEdge >( this, faceIdx, i );
		}
	}


	void HelperObjectVisConvexMesh::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& debugDrawer ) const
	{
		const EditModeViewport viewport( camera, debugDrawer );
		rend::FragmentBuilder builder( debugDrawer );

		for ( const auto& edges : m_edges )
		{
			for ( const auto& edge : edges )
			{
				edge->OnRender( viewport, builder );
			}
		}

		for ( const auto& vertices : m_vertices )
		{
			for ( const auto& vert : vertices )
			{
				vert->OnRender( viewport, builder );
			}
		}

		for ( const auto& face : m_faces )
		{
			face->OnRender( viewport, builder );
		}

		if ( m_addVertexPreview.m_enable )
		{
			builder.SetColor( Color::GREEN );

			const auto& faceVerts = GetOccluderNode()->GetFaceVertices( m_addVertexPreview.m_faceIndex );

			const Vector& pointA = faceVerts[m_addVertexPreview.m_index];
			const Vector& pointB = faceVerts[( m_addVertexPreview.m_index + 1 ) % faceVerts.Size()];
			Vector3 pos = math::Lerp( m_addVertexPreview.m_position, pointA, pointB );
			pos = GetTransform().TransformPoint( pos );

			Float screenSpaceScale = viewport.CalcScreenSpaceScale( pos );
			Float controlPointSize = math::Max( 0.0f, screenSpaceScale * 0.04f );
			const Vector3 extrude = Vector3( controlPointSize, controlPointSize, controlPointSize );

			builder.AddSolidBox( pos - extrude, pos + extrude );
		}

		if ( m_addFacePreview.m_enable )
		{
			builder.SetColor( Color::GREEN );
			builder.SetLocalTransform( GetTransform() );

			const red::DynArray<Vector3> faceVerts = world::StaticOccluderConvexMeshNode::CreateDefaultFace( m_addFacePreview.m_center, m_addFacePreview.m_normal );

			builder.AddSolidPolygon( faceVerts.TypedData(), faceVerts.Size() );
			if ( GetOccluderNode()->GetTwoSided() )
			{
				builder.AddSolidPolygon( faceVerts.TypedData(), faceVerts.Size(), true );
			}
		}
	}


	HelperObjectVisConvexMeshVertexPtr HelperObjectVisConvexMesh::GetVertexSubobject( Uint16 faceIdx, Uint16 vertIdx ) const
	{
		if ( faceIdx >= m_vertices.Size() || vertIdx >= m_vertices[faceIdx].Size() )
		{
			return nullptr;
		}
		return m_vertices[faceIdx][vertIdx];
	}

	HelperObjectVisConvexMeshEdgePtr HelperObjectVisConvexMesh::GetEdgeSubobject( Uint16 faceIdx, Uint16 edgeIdx ) const
	{
		if ( faceIdx >= m_edges.Size() || edgeIdx >= m_edges[faceIdx].Size() )
		{
			return nullptr;
		}
		return m_edges[faceIdx][edgeIdx];
	}

	HelperObjectVisConvexMeshFacePtr HelperObjectVisConvexMesh::GetFaceSubobject( Uint16 faceIdx ) const
	{
		if ( faceIdx >= m_faces.Size() )
		{
			return nullptr;
		}
		return m_faces[faceIdx];
	}


	Bool HelperObjectVisConvexMesh::GetSelectedVertices( red::DynArray< HelperObjectVisConvexMeshVertex* >& outSelectedVertices ) const
	{
		for ( const auto& points : m_vertices )
		{
			for ( const auto& point : points )
			{
				if ( point->m_isSelected )
				{
					outSelectedVertices.PushBack( point.Get() );
				}
			}
		}

		return !outSelectedVertices.Empty();
	}

	Bool HelperObjectVisConvexMesh::GetSelectedEdges( red::DynArray< HelperObjectVisConvexMeshEdge* >& outSelectedEdges ) const
	{
		for ( const auto& edges : m_edges )
		{
			for ( const auto& edge : edges )
			{
				if ( edge->m_isSelected )
				{
					outSelectedEdges.PushBack( edge.Get() );
				}
			}
		}

		return !outSelectedEdges.Empty();
	}

	Bool HelperObjectVisConvexMesh::GetSelectedFaces( red::DynArray< HelperObjectVisConvexMeshFace* >& outSelectedFaces ) const
	{
		for ( const auto& face : m_faces )
		{
			if ( face->m_isSelected )
			{
				outSelectedFaces.PushBack( face.Get() );
			}
		}

		return !outSelectedFaces.Empty();
	}


	Bool HelperObjectVisConvexMesh::DeleteSelected( SelectionHandler* selectionHandler )
	{
		auto node = GetOccluderNode();
		red::DynArray< HelperObjectVisConvexMeshVertex* > selectedVertices;
		red::DynArray< HelperObjectVisConvexMeshEdge* > selectedEdges;
		red::DynArray< HelperObjectVisConvexMeshFace* > selectedFaces;
		GetSelectedVertices( selectedVertices );
		GetSelectedEdges( selectedEdges );
		GetSelectedFaces( selectedFaces );

		// Put all vertices to remove into an ordered Set. The ordering is from last-to-first (vertex and face indices),
		// so that we can go through them in order and not worry about messing up later indices.

		struct VertToRemove
		{
			Uint32 faceIndex;
			Uint32 vertIndex;

			VertToRemove(Uint32 face, Uint32 vert)
				: faceIndex(face)
				, vertIndex(vert)
			{}
		};
		struct CustomSortPredicate
		{
			RED_INLINE Bool operator()( const VertToRemove& a, const VertToRemove& b ) const
			{
				if ( a.faceIndex != b.faceIndex )
				{
					return a.faceIndex > b.faceIndex;
				}
				return a.vertIndex > b.vertIndex;
			}
		};
		red::Set< VertToRemove, CustomSortPredicate > vertsToRemove;

		for ( const HelperObjectVisConvexMeshVertex* point : selectedVertices )
		{
			vertsToRemove.Insert( VertToRemove( point->m_faceIndex, point->m_vertIndex ) );
		}

		for ( const HelperObjectVisConvexMeshEdge* edge : selectedEdges )
		{
			const auto& faceVerts = node->GetFaceVertices( edge->m_faceIndex );

			vertsToRemove.Insert( VertToRemove( edge->m_faceIndex, edge->m_edgeIndex ) );
			vertsToRemove.Insert( VertToRemove( edge->m_faceIndex, ( edge->m_edgeIndex + 1 ) % faceVerts.Size() ) );
		}

		for ( const HelperObjectVisConvexMeshFace* face : selectedFaces )
		{
			const auto& faceVerts = node->GetFaceVertices( face->m_faceIndex );
			for ( Uint32 i = 0; i < faceVerts.Size(); ++i )
			{
				vertsToRemove.Insert( VertToRemove( face->m_faceIndex, i ) );
			}
		}

		if ( vertsToRemove.Size() == 0 )
		{
			return true;
		}


		Int32 lastFaceIndex = -1;
		Uint32 lastFaceSize = 0;
		Uint32 numFaces = node->GetNumFaces();

		red::DynArray< Action_VisConvexMeshDeleteVertex::Info > deleteInfo;
		for ( const auto& toRemove : vertsToRemove )
		{
			const Uint32 faceIndex = toRemove.faceIndex;
			const Uint32 vertIndex = toRemove.vertIndex;

			const auto& faceVerts = node->GetFaceVertices(faceIndex);

			if ( lastFaceIndex == -1 || lastFaceIndex != faceIndex )
			{
				lastFaceIndex = faceIndex;
				lastFaceSize = faceVerts.Size();
			}

			// If there will only be one face left, don't let allow it to go smaller than 3 verts.
			if ( numFaces == 1 && lastFaceSize <= 3 )
			{
				break;
			}

			const Vector3 previousPosition = faceVerts[toRemove.vertIndex];
			deleteInfo.PushBack( Action_VisConvexMeshDeleteVertex::Info( previousPosition, faceIndex, vertIndex ) );

			RED_ASSERT( lastFaceSize > 0, "" );
			--lastFaceSize;

			// If this is the last vertex to remove from the face, remove the face.
			// #TODO: Actually, need to do if the face is down to under 3 verts... is a bit more complicated than that,
			// since it may involve deleting vertices that have now had their indices changed...
			if ( lastFaceSize == 0 )
			{
				deleteInfo.PushBack( Action_VisConvexMeshDeleteVertex::Info(Action_VisConvexMeshDeleteVertex::Info::DELETE_FACE, faceIndex) );
				RED_ASSERT( numFaces > 1, "Deleted the last face! Shouldn't happen!" );
				--numFaces;
			}
		}

		if ( deleteInfo.Size() > 0 )
		{
			m_actionHistory->Execute( new Action_VisConvexMeshDeleteVertex( "Convex occluder shape delete point", selectionHandler, deleteInfo ));
			return true;
		}

		return false;
	}

	Bool HelperObjectVisConvexMesh::OnMouseMoved( PreviewPanel& panel, const InputMouseMove& evt )
	{
		return false;
	}

	Bool HelperObjectVisConvexMesh::OnKeyDown( PreviewPanel& panel, const InputKeyEvent& evt, SelectionHandler* selectionHandler )
	{
		if ( evt.m_key == prv::enterAddObjectModeKey )
		{
			panel.AttachInputHandler( new InputHandler_VisConvexMeshAddObject( this, panel, &m_addVertexPreview, &m_addFacePreview, selectionHandler, m_actionHistory ) );
			return true;
		}

		return false;
	}


	Color HelperObjectVisConvexMesh::GetSubobjectColor( Bool isSelected, Bool isRelatedSelected )
	{
		static const Color colors[] = {
			Color(192, 63, 63),			// not selected, related not selected
			Color(255, 255, 0),			// selected, related not selected
			Color(127, 127, 63),		// not selected, related selected
			Color(255, 255, 0),			// selected, related selected
		};
		const Uint32 idx = (isSelected ? 1 : 0) + (isRelatedSelected ? 2 : 0);
		return colors[idx];
	}


}

#endif