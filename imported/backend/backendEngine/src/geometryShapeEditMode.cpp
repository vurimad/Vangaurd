/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "geometryShapeEditMode.h"

#include "editAction.h"
#include "editActionHistory.h"
#include "genericPreviewPanelSharedData.h"
#include "helperObjectFactory.h"
#include "objectPersistentCollection.h"
#include "previewEditMode.h"
#include "previewPanel.h"
#include "../../common/core/include/message.h"
#include "../../common/core/include/polygonTriangulation.h"
#include "../../common/renderDraw/include/fragmentBuilder.h"
#include "../../common/triggers/include/geometryShape.h"
#include "../../common/world/include/worldGeometryShapeNode.h"
#include "../../common/redReflection/include/objectUtils.hpp"
#include "../../common/redReflection/include/mathUtils.h"
#include "../../common/renderData/include/renderSelectionResult.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( GeoVertex, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_id );
RTTI_PROPERTY( m_position );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( GeoEdge, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_id );
RTTI_PROPERTY( m_vertexStart );
RTTI_PROPERTY( m_vertexEnd );
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( GeoFace, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_id );
RTTI_PROPERTY( m_vertices );
RTTI_END_TYPE();

namespace red
{
	THash32 GetHash( const tools::GeometryData::Edge& face )
	{
		return red::CombineHashes32( face.m_vertexStart, face.m_vertexEnd );
	}
}

namespace tools
{
	namespace {
		const Bool RegisterHelperObjectCreatorForGeometryShapeNodes()
		{
			const tools::HelperObjectFactory::Creator::FactoryFunc func = []( const tools::EditableProxyPtr& proxy ) -> tools::HelperObjectPtr
			{
				const THandle< ISerializable > object = proxy->GetSerializable();
				THandle< world::GeometryShapeNode > node = Cast< world::GeometryShapeNode >( object );
				RED_FATAL_ASSERT( node, "Geometry shape node expected" );

				red::UniquePtr< tools::GeometryShapeEditMode > helper = red::CreateUniquePtr< tools::GeometryShapeEditMode >( proxy );

				return std::move( helper );
			};

			const tools::HelperObjectFactory::Creator creator( world::GeometryShapeNode::GetStaticClass(), func, 9u );
			return tools::HelperObjectFactory::RegisterCreator( creator );
		}

		static RTTIRegistrator g_helperObjectCreatorForGeometryShapeNodes_registrator( []() {}, []() { RegisterHelperObjectCreatorForGeometryShapeNodes(); } );

		Vector3 ToWorld( GeometryShapeEditMode* mode, const Vector3& point )
		{
			const Vector3& scale = mode->GetScale();
			const Transform& transform = mode->GetTransform();

			return transform.TransformPoint( scale * point );
		}

		Vector3 ToLocal( GeometryShapeEditMode* mode, const Vector3& point )
		{
			const Vector3& scale = mode->GetScale();
			const Transform& transform = mode->GetInverseTransform();

			return transform.TransformPoint( point ) / scale;
		}
	} // namespace


	// Fastest thing to write, far from optimal in terms of memory though
	class GenericGeometryShapeEditModeAction: public IEditAction
	{
	public:
		GenericGeometryShapeEditModeAction( PreviewPanelSharedData* sharedData, String actionDescription, std::function<Bool()> action )
			: m_action( action )
			, m_sharedData( static_cast< GenericPreviewPanelSharedData* >( sharedData ) )
			, m_description( actionDescription )
		{
			GeometryShapeEditMode* editMode = m_sharedData->GetObjectHandler().FindObjectResolver< GeometryShapeEditMode >();
			RED_ASSERT( editMode, "This action can only be called from GeometryShapeEditMode" );

			m_capturedShapeBefore = editMode->CaptureAll();
			m_oldSelection = editMode->GetSelection();
		}

		virtual ~GenericGeometryShapeEditModeAction() override = default;

		virtual String GetDescription() const override
		{
			return m_description;
		}

		virtual Bool Do() override
		{
			GeometryShapeEditMode* editMode = m_sharedData->GetObjectHandler().FindObjectResolver< GeometryShapeEditMode >();
			RED_ASSERT( editMode, "This action can only be called from GeometryShapeEditMode" );

			if ( m_capturedShapeAfter )
			{
				if ( editMode->RestoreAll( m_capturedShapeAfter ) )
				{
					editMode->SelectObjects( m_newSelection );
					return true;
				}
				return false;
			}
			else
			{
				if ( m_action() )
				{
					m_capturedShapeAfter = editMode->CaptureAll();
					m_newSelection = editMode->GetSelection();
					return true;
				}
				return false;
			}
		}

		virtual Bool Undo() override
		{
			GeometryShapeEditMode* editMode = m_sharedData->GetObjectHandler().FindObjectResolver< GeometryShapeEditMode >();
			RED_ASSERT( editMode, "This action can only be called from GeometryShapeEditMode" );

			editMode->RestoreAll( m_capturedShapeBefore );
			editMode->SelectObjects( m_newSelection );

			return true;
		}

		virtual Bool IsModification() const override { return false; }

	private:
		std::function<Bool()> m_action;
		GenericPreviewPanelSharedData* m_sharedData;
		red::SharedPtr< IObjectPersistentCollection > m_capturedShapeBefore;
		red::SharedPtr< IObjectPersistentCollection > m_capturedShapeAfter;
		red::DynArray< EditorObjectID > m_oldSelection;
		red::DynArray< EditorObjectID > m_newSelection;
		String m_description;
	};

	class ExtrudeInputHandler: public IPreviewCommand
	{
	public:
		ExtrudeInputHandler( GeometryShapeEditMode* mesh, EditorObjectID objectId );
		~ExtrudeInputHandler();

		virtual void OnCanceled() override;
		virtual void OnFinished() override;

		virtual void OnMouseDown( const InputMouseButton& evt, InputResult& result ) override;
		virtual void OnMouseMoved( const InputMouseMove& evt, InputResult& result ) override;
		virtual void OnKeyDown( const InputKeyEvent& evt, InputResult& result ) override;
		virtual void OnKeyUp( const InputKeyEvent& evt, InputResult& result ) override;

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override;

	private:
		GeometryShapeEditMode* GetMesh() const { return m_mesh; }

		GeometryShapeEditMode* m_mesh;
		EditorObjectID m_objectId;
	};

	ExtrudeInputHandler::ExtrudeInputHandler( GeometryShapeEditMode* mesh, EditorObjectID objectId )
		: m_mesh( mesh )
		, m_objectId( objectId )
	{
	}

	ExtrudeInputHandler::~ExtrudeInputHandler() = default;

	void ExtrudeInputHandler::OnCanceled()
	{
	}

	void ExtrudeInputHandler::OnFinished()
	{
		m_mesh->ProcessChanges();
	}

	void ExtrudeInputHandler::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{
		Vector3 normal = GetMesh()->GetFaceNormal( m_objectId );
		Vector3 center = GetMesh()->GetFacePosition( m_objectId );

		// calculate box size (point representation)
		center = ToWorld( GetMesh(), center );

		rend::FragmentBuilder builder( dd );

		builder.SetColor( Color::RED() );
		builder.EnableOverlay( true );
		builder.AddLine( center, center + ( normal * 205 ) );

		{
			String selectionMode = "Face extrude mode";
			builder.AddFlatText( 25, 50, selectionMode );
		}
	}

	void ExtrudeInputHandler::OnMouseDown( const InputMouseButton& evt, InputResult& result )
	{
		result.m_handled = false;
		result.m_keepLive = true;
	}

	void ExtrudeInputHandler::OnMouseMoved( const InputMouseMove& evt, InputResult& result )
	{
		result.m_handled = false;
		result.m_keepLive = true;
	}

	void ExtrudeInputHandler::OnKeyDown( const InputKeyEvent& evt, InputResult& result )
	{
		result.m_handled = false;
		result.m_keepLive = true;
	}

	void ExtrudeInputHandler::OnKeyUp( const InputKeyEvent& evt, InputResult& result )
	{
		result.m_handled = false;
		result.m_keepLive = true;
	}

	///
	///
	///

	class EdgeSubdivideInputHandler : public IPreviewCommand
	{
	public:
		EdgeSubdivideInputHandler( GeometryShapeEditMode* mesh, EditorObjectID edge1Id, EditorObjectID edge2Id );
		~EdgeSubdivideInputHandler();

		virtual void OnCanceled() override;
		virtual void OnFinished() override;

		virtual void OnMouseDown( const InputMouseButton& evt, InputResult& result ) override;
		virtual void OnMouseMoved( const InputMouseMove& evt, InputResult& result ) override;
		virtual void OnKeyDown( const InputKeyEvent& evt, InputResult& result ) override;
		virtual void OnKeyUp( const InputKeyEvent& evt, InputResult& result ) override;

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override;

	private:
		Bool CalculateObjectPlacement( const EditModeViewport& vpt, Int32 x, Int32 y, Vector3& outWorldPosition ) const;
		Bool CalculateObjectPlacementLocation( const Vector3& rayOrigin, const Vector3& rayDirection, Vector3& outLocation ) const;

		GeometryShapeEditMode* GetMesh() const { return m_mesh; }

		GeometryShapeEditMode* m_mesh;

		EditorObjectID m_edge1Id;
		EditorObjectID m_edge2Id;
		Vector3 m_startPoint[ 2 ];
		Vector3 m_endPoint[ 2 ];
		Vector3 m_direction[ 2 ];
		Float m_length[ 2 ];

		Float m_factor{ 0.5f };

		red::SharedPtr< rend::DepthResult > m_depthMap;

		Vector3 m_aPoint;
	};

	EdgeSubdivideInputHandler::EdgeSubdivideInputHandler( GeometryShapeEditMode* mesh, EditorObjectID edge1Id, EditorObjectID edge2Id )
		: m_mesh( mesh )
		, m_edge1Id( edge1Id )
		, m_edge2Id( edge2Id )
	{			
		m_depthMap = red::CreateSharedPtr< rend::DepthResult >( red::PoolBackend() );
		auto normalMap = red::CreateSharedPtr< rend::Color10Result >( red::PoolBackend() );

		const red::DynArray< PreviewPanel* >& panels = mesh->GetPreviewPanelSharedData()->GetPreviewPanels();
		PreviewPanel* panel = panels[ 0 ];
		Rect rect( 0, panel->GetWidth(), 0,  panel->GetHeight() );

		panel->RenderDepthMap( rect, m_depthMap, nullptr );

		GetMesh()->GetEdgeData( m_edge2Id, m_direction[ 0 ], m_length[ 0 ], m_startPoint[ 0 ], m_endPoint[ 0 ] );
		GetMesh()->GetEdgeData( m_edge1Id, m_direction[ 1 ], m_length[ 1 ], m_startPoint[ 1 ], m_endPoint[ 1 ] );

		if ( m_startPoint[ 0 ].Z < m_endPoint[ 0 ].Z )
		{
			Vector3 tmp = m_startPoint[ 0 ];
			m_startPoint[ 0 ] = m_endPoint[ 0 ];
			m_endPoint[ 0 ] = tmp;

			m_direction[ 0 ] = -m_direction[ 0 ];
		}

		if ( m_startPoint[ 1 ].Z < m_endPoint[ 1 ].Z )
		{
			Vector3 tmp = m_startPoint[ 1 ];
			m_startPoint[ 1 ] = m_endPoint[ 1 ];
			m_endPoint[ 1 ] = tmp;

			m_direction[ 1 ] = - m_direction[ 1 ];
		}
	}

	EdgeSubdivideInputHandler::~EdgeSubdivideInputHandler() = default;

	void EdgeSubdivideInputHandler::OnCanceled()
	{
	}

	void EdgeSubdivideInputHandler::OnFinished()
	{
		m_mesh->ProcessChanges();
	}

	void EdgeSubdivideInputHandler::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{	
		Vector3 firstVertexPosition = m_startPoint[ 0 ] + m_direction[ 0 ] * m_factor * m_length[ 0 ];
		Vector3 secondVertexPosition = m_startPoint[ 1 ] + m_direction[ 1 ] * m_factor * m_length[ 1 ];

		firstVertexPosition = ToWorld( GetMesh(), firstVertexPosition );
		secondVertexPosition = ToWorld( GetMesh(), secondVertexPosition );

		rend::FragmentBuilder builder( dd );

		builder.SetColor( Color::RED() );
		builder.EnableOverlay( true );
		builder.AddLine( firstVertexPosition, secondVertexPosition );

		{
			String selectionMode = "Add edge mode";
			builder.AddFlatText( 25, 50, selectionMode );
		}
	}

	Bool EdgeSubdivideInputHandler::CalculateObjectPlacement( const EditModeViewport& vpt, Int32 x, Int32 y, Vector3& outWorldPosition ) const
	{
		if ( m_depthMap && m_depthMap->CalcWorldSpacePoint( x, y, outWorldPosition ) )
		{
			return true;
		}

		// compute ray for geometry checks
		Vector3 rayOrigin, rayDirection;
		vpt.CalcRay( x, y, rayOrigin, rayDirection );

		if ( CalculateObjectPlacementLocation( rayOrigin, rayDirection, outWorldPosition ) )
		{
			return true;
		}

		return false;
	}

	Bool EdgeSubdivideInputHandler::CalculateObjectPlacementLocation( const Vector3& rayOrigin, const Vector3& rayDirection, Vector3& outLocation ) const
	{
		// calculate box size (point representation)
		Vector3 start = ToWorld( GetMesh(), m_startPoint[ 0 ] );
		Vector3 end = ToWorld( GetMesh(), m_endPoint[ 0 ] );

		const Float maxRayDist = 5000.0f;

		Vector4 pointOnSegment, pointOnRay;

		// find nearest point between ray and line section
		if ( MathUtils::GeometryUtils::DistanceLineLine3D( end, start, rayOrigin, rayOrigin + rayDirection * maxRayDist, pointOnSegment, pointOnRay ) )
		{
			const Float edgeSqrLen = ( start - end ).SquareMag();
			outLocation = pointOnSegment;

			// check, if the new point is on the edge
			if ( ( pointOnSegment - start ).SquareMag3() >= edgeSqrLen )
			{
				return false;
			}

			if ( ( pointOnSegment - end ).SquareMag3() >= edgeSqrLen )
			{
				return false;
			}

			return true;
		}

		return false;
	}

	void EdgeSubdivideInputHandler::OnMouseDown( const InputMouseButton& evt, InputResult& result )
	{
		if ( evt.m_button == LMB )
		{
			EditorObjectID edgeId;
			EditorObjectID vertex1Id;
			EditorObjectID vertex2Id;

			GetMesh()->EdgeSubdivide( m_edge1Id, m_factor, vertex1Id, edgeId );
			GetMesh()->EdgeSubdivide( m_edge2Id, m_factor, vertex2Id, edgeId );
		
			GetMesh()->ConnectVerticesByEdge( vertex1Id, vertex2Id, edgeId );

			GetMesh()->SelectObjects( { edgeId } );

			result.m_handled = true;
			result.m_keepLive = false;

			return;
		}

		result.m_handled = false;
		result.m_keepLive = true;
	}

	void EdgeSubdivideInputHandler::OnMouseMoved( const InputMouseMove& evt, InputResult& result )
	{
		Vector3 worldSpacePosition;

		Bool isPreviewValid = CalculateObjectPlacement( evt.m_viewport, evt.m_x, evt.m_y, worldSpacePosition );
		if ( isPreviewValid )
		{
			// Smarter 3D Modeling with Edge Loops
			Float distance = m_startPoint[ 0 ].DistanceTo( ToLocal( GetMesh(), worldSpacePosition ) );
			Float fullDistance = m_startPoint[ 0 ].DistanceTo( m_endPoint[ 0 ] );

			m_factor = distance / fullDistance;
			m_factor = Clamp( m_factor, 0.0f, 1.0f );
		}
		else
		{
			// Smarter 3D Modeling with Edge Loops
			Float distanceToStart = m_startPoint[ 0 ].DistanceTo( ToLocal( GetMesh(), worldSpacePosition ) );
			Float distanceToEnd = m_endPoint[ 0 ].DistanceTo( ToLocal( GetMesh(), worldSpacePosition ) );

			Float fullDistance = m_startPoint[ 0 ].DistanceTo( m_endPoint[ 0 ] );

			if ( distanceToStart <= distanceToEnd )
			{
				m_factor = 0;
			}
			else
			{
				m_factor = 1;
			}
		}

		result.m_handled = false;
		result.m_keepLive = true;
	}

	void EdgeSubdivideInputHandler::OnKeyDown( const InputKeyEvent& evt, InputResult& result )
	{
		result.m_handled = false;
		result.m_keepLive = true;
	}

	void EdgeSubdivideInputHandler::OnKeyUp( const InputKeyEvent& evt, InputResult& result )
	{
		result.m_handled = false;
		result.m_keepLive = true;
	}

	///
	///
	///

	const CName GeometryShapeEditMode::s_vertexTypeName = RED_NAME_CONSTEXPR( "vertex" );
	const CName GeometryShapeEditMode::s_edgeTypeName = RED_NAME_CONSTEXPR( "edge" );
	const CName GeometryShapeEditMode::s_faceTypeName = RED_NAME_CONSTEXPR( "face" );

	GeometryShapeEditMode::GeometryShapeEditMode( const tools::EditableProxyPtr& proxy )
		: PreviewEditModeAdapter( proxy )
	{
		Intialize( Cast< world::GeometryShapeNode >( proxy->GetSerializable() )->GetShape() );
	}

	GeometryShapeEditMode::~GeometryShapeEditMode() = default;

	void GeometryShapeEditMode::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& debugDrawer ) const
	{
		const EditModeViewport viewport( camera, debugDrawer );

		rend::FragmentBuilder builder( debugDrawer );
		builder.EnableTransparency( true );
		builder.EnableOverlay( false );

		{
			// Front
			for ( const PreviewObjectPtr& iter : m_previewObjects )
			{
				if ( iter->GetEditorObjectID().GetKind() == s_faceTypeName )
				{
					iter->OnRender( viewport, builder, false );
				}
			}
		}

		{
			// Back
			for ( const PreviewObjectPtr& iter : m_previewObjects )
			{
				if ( iter->GetEditorObjectID().GetKind() == s_faceTypeName )
				{
					iter->OnRender( viewport, builder, true );
				}
			}
		}


		{
			const Bool overlay = true;
			builder.EnableOverlay( overlay );
			for ( const PreviewObjectPtr& iter : m_previewObjects )
			{
				if ( iter->GetEditorObjectID().GetKind() != s_faceTypeName )
				{
					iter->OnRender( viewport, builder, overlay );
				}
			}
		}

		{
			const Bool overlay = false;
			builder.EnableTransparency( false );
			builder.EnableOverlay( overlay );
			for ( const PreviewObjectPtr& iter : m_previewObjects )
			{
				if ( iter->GetEditorObjectID().GetKind() != s_faceTypeName )
				{
					iter->OnRender( viewport, builder, overlay );
				}
			}
		}

		RenderInfo( builder );
	}

	void GeometryShapeEditMode::RenderInfo( rend::FragmentBuilder& builder ) const
	{
		{
			String selectionMode = "Selection mode: ";
			if ( m_selectionMode == GeoSelectionMode::Vertices )
			{
				selectionMode += "vertex";
			}
			else if ( m_selectionMode == GeoSelectionMode::Edges )
			{
				selectionMode += "edge";
			}
			else if ( m_selectionMode == GeoSelectionMode::Faces )
			{
				selectionMode += "face";
			}
			else
			{
				selectionMode += "all";
			}

			builder.AddFlatText( 25, 25, selectionMode );
		}

		red::DynArray< String > help;
		help.PushBack( "ctrl+1,2,3,4 selection mode" );
		help.PushBack( "ctrl+k selected edge cut" );
		help.PushBack( "ctrl+l create edge between two vertices" );
		help.PushBack( "shift+l subdivide edges and create a new edge between them" );
		help.PushBack( "ctrl+e extrude selected face" );
		help.PushBack( "ctrl+r loop cut starting from selected edge" );
		help.PushBack( "ctrl+d dissolve selected edge or vertex" );
		help.PushBack( "y snap selected vertex to a geometry" );
	
		builder.SetColor( Color::BLACK() );
		builder.EnableOverlay( true );

		Uint32 index = 0;
		for ( const String& item : help )
		{
			builder.AddFlatText( 25, 75 + index * 20 , item.AsChar() );
			++index;
		}
	}

	void GeometryShapeEditMode::Intialize( const THandle< GeometryShape >& shape )
	{
		Bool cleanedUp = false;
		if ( !shape->Verify() )
		{
			shape->FixDuplicatedVertices();
			shape->FixFreeHangingVertices();
			shape->Editor_BuildIndices();

			cleanedUp = true;
		}

		GeometryData data;
		data.m_vertices = shape->GetVertices();
		red::HashSet< GeometryData::Edge > addedEdges{ red::PoolBackend() };

		for ( const GeometryShapeFace& face: shape->GetFaces() )
		{
			GeometryData::Face& dataFace = data.m_faces.EmplaceBack();
			Uint32 j = face.m_indices.Size() - 1;
			for ( Uint32 i = 0u; i < face.m_indices.Size(); ++i )
			{
				GeometryData::Edge edge{ Min( face.m_indices[ i ], face.m_indices[ j ] ), Max( face.m_indices[ i ], face.m_indices[ j ] ) };
				if ( !addedEdges.Exist( edge ) )
				{
					addedEdges.Insert( edge );
					data.m_edges.PushBack( edge );
				}
				dataFace.m_vertices.PushBack( face.m_indices[ i ] );
				j = i;
			}
		}

		Intialize( data );
		if ( cleanedUp )
		{
			UpdateGeometryShape();
		}
	}

	void GeometryShapeEditMode::Intialize( const GeometryData& data )
	{
		CreateVerticesObjects( data );
		CreateEdgesObjects( data );
		CreateFacesObjects( data );
	}

	void GeometryShapeEditMode::UpdateGeometryShape()
	{
		// Instead serialize shape and apply via proxy to m_shape
		THandle< world::GeometryShapeNode > node = Cast< world::GeometryShapeNode >( GetProxy()->GetSerializable() );
		THandle< GeometryShape > shape = CreateHandle< GeometryShape >();

		red::HashMap< EditorObjectID, Uint32 > vertexToIndex{ red::PoolBackend() };

		{
			red::DynArray< Vector3 > outVertices{ red::PoolBackend() };
			outVertices.Clear();
			outVertices.Reserve( m_vertices.Size() );
			for ( const auto& vertex : m_vertices )
			{
				vertexToIndex.Insert( vertex.Key(), outVertices.Size() );
				outVertices.PushBack( vertex.Value().m_position );
			}
			shape->Editor_SetVertices( outVertices );
		}

		{
			red::DynArray< GeometryShapeFace > outFaces{ red::PoolBackend() };
			outFaces.Clear();
			outFaces.Reserve( m_faces.Size() );
			for ( const auto& face: m_faces )
			{
				auto& outFace = outFaces.EmplaceBack();
				outFace.m_indices.Reserve( face.Value().m_vertices.Size() );
				for ( const auto& faceVertex : face.Value().m_vertices )
				{
					outFace.m_indices.PushBack( vertexToIndex[ faceVertex ] );
				}
			}
			shape->Editor_SetFaces( outFaces );
		}

		shape->Editor_BuildIndices();
		node->SetShape( shape );
	}

	void GeometryShapeEditMode::CreateVerticesObjects( const GeometryData& data )
	{
		m_vertexGeneratorId = 0;

		for ( const Vector3& vertex : data.m_vertices )
		{
			EditorObjectID id = CreateVertex();
			SetVertexPosition( id, vertex );
		}
	}

	void GeometryShapeEditMode::CreateEdgesObjects( const GeometryData& data )
	{
		m_edgeGeneratorId = 0;

		for ( const GeometryData::Edge& edge : data.m_edges )
		{
			EditorObjectID id = CreateEdge();
			SetEdgeVertices( id, CreateVertexId( edge.m_vertexStart ), CreateVertexId( edge.m_vertexEnd ) );
		}
	}

	void GeometryShapeEditMode::CreateFacesObjects( const GeometryData& data )
	{
		m_faceGeneratorId = 0;

		for ( const GeometryData::Face& face : data.m_faces )
		{
			EditorObjectID id = CreateFace();

			red::DynArray< EditorObjectID > vertices;
			for ( Uint32 vertexId : face.m_vertices )
			{
				vertices.PushBack( CreateVertexId( vertexId ) );
			}

			SetFaceVertices( id, vertices );
		}
	}

	void GeometryShapeEditMode::CaptureDeleteData( const EditorObjectID& id, red::DynArray< PreviewObjectHolderPtr >& deleteData ) const
	{
		if ( id.GetKind() == s_vertexTypeName )
		{
			for ( const auto& iter : m_edges )
			{
				const GeoEdge& edge = iter.Value();
				if ( edge.m_vertexEnd == id || edge.m_vertexStart == id )
				{
					PreviewObjectHolderPtr dataHolder = CreateHandle< PreviewObjectHolder >();
					dataHolder->m_editorID = edge.m_id;

					THandle< GeoEdge > data = CreateHandle< GeoEdge >();
					data->m_id = edge.m_id;
					data->m_vertexStart = edge.m_vertexStart;
					data->m_vertexEnd = edge.m_vertexEnd;

					dataHolder->m_data = data;

					deleteData.PushBack( dataHolder );
				}
			}

			const GeoVertex& vertex = m_vertices[ id ];

			PreviewObjectHolderPtr dataHolder = CreateHandle< PreviewObjectHolder >();
			dataHolder->m_editorID = id;

			THandle< GeoVertex > data = CreateHandle< GeoVertex >();
			data->m_id = vertex.m_id;
			data->m_position = vertex.m_position;

			dataHolder->m_data = data;

			deleteData.PushBack( dataHolder );
		}
		else if ( id.GetKind() == s_edgeTypeName )
		{
			const GeoEdge& edge = m_edges[ id ];

			PreviewObjectHolderPtr dataHolder = CreateHandle< PreviewObjectHolder >();
			dataHolder->m_editorID = id;

			THandle< GeoEdge > data = CreateHandle< GeoEdge >();
			data->m_id = edge.m_id;
			data->m_vertexStart = edge.m_vertexStart;
			data->m_vertexEnd = edge.m_vertexEnd;

			dataHolder->m_data = data;

			deleteData.PushBack( dataHolder );
		}
		else if ( id.GetKind() == s_faceTypeName )
		{
			const GeoFace& face = m_faces[ id ];

			PreviewObjectHolderPtr dataHolder = CreateHandle< PreviewObjectHolder >();
			dataHolder->m_editorID = id;

			THandle< GeoFace > data = CreateHandle< GeoFace >();
			data->m_id = face.m_id;
			data->m_vertices = face.m_vertices;

			dataHolder->m_data = data;

			deleteData.PushBack( dataHolder );
		}
	}

	void GeometryShapeEditMode::RestoreObjects( const red::DynArray< PreviewObjectHolderPtr >& objects )
	{
		for ( const PreviewObjectHolderPtr& item : objects )
		{
			if ( item->GetEditorObjectID().GetKind() == s_vertexTypeName )
			{
				THandle< GeoVertex > vertex = Cast< GeoVertex >( item->m_data );

				CreateVertex( vertex->m_id );
				SetVertexPosition( vertex->m_id, vertex->m_position );
			}
		}

		for ( const PreviewObjectHolderPtr& item : objects )
		{
			if ( item->GetEditorObjectID().GetKind() == s_edgeTypeName )
			{
				THandle< GeoEdge > edge = Cast< GeoEdge >( item->m_data );

				CreateEdge( edge->m_id );
				SetEdgeVertices( edge->m_id, edge->m_vertexStart, edge->m_vertexEnd );
			}
		}

		for ( const PreviewObjectHolderPtr& item : objects )
		{
			if ( item->GetEditorObjectID().GetKind() == s_faceTypeName )
			{
				THandle< GeoFace > edge = Cast< GeoFace >( item->m_data );

				CreateFace( edge->m_id );
				SetFaceVertices( edge->m_id, edge->m_vertices );
			}
		}
	}

	void GeometryShapeEditMode::RemoveObjects( const red::DynArray< EditorObjectID >& objects )
	{
		for ( const auto& object : objects )
		{
			if ( object.GetKind() == s_vertexTypeName )
			{
				RemoveVertex( object );
			}

			if ( object.GetKind() == s_edgeTypeName )
			{
				RemoveEdge( object );
			}

			if ( object.GetKind() == s_faceTypeName )
			{
				RemoveFace( object );
			}
		}
	}

	GeoSelectionMode GeometryShapeEditMode::GetSelectionMode() const
	{
		return m_selectionMode;
	}

	void GeometryShapeEditMode::SetSelectionMode( GeoSelectionMode mode )
	{
		m_selectionMode = mode;
	}

	Bool GeometryShapeEditMode::IsSelectionModeEnabled( GeoSelectionMode mode ) const
	{
		return m_selectionMode == GeoSelectionMode::All || mode == m_selectionMode;
	}

	Bool GeometryShapeEditMode::SupportsContentType( const CName& contentTypeName ) const
	{
		if ( contentTypeName == s_vertexTypeName ||
			contentTypeName == s_edgeTypeName ||
			contentTypeName == s_faceTypeName )
		{
			return true;
		}

		return false;
	}

	Bool GeometryShapeEditMode::CanBeResolved( const EditorObjectID& id ) const
	{
		static red::StaticArray< CName, 3u > s_allowedTypes{ s_vertexTypeName, s_edgeTypeName, s_faceTypeName };
		if ( m_selectionMode == GeoSelectionMode::All && s_allowedTypes.Exist( id.GetKind() ) )
		{
			return true;
		}
		else if ( m_selectionMode == GeoSelectionMode::Vertices && id.GetKind() == s_vertexTypeName )
		{
			return true;
		}
		else if ( m_selectionMode == GeoSelectionMode::Edges && id.GetKind() == s_edgeTypeName )
		{
			return true;
		}
		else if ( m_selectionMode == GeoSelectionMode::Faces && id.GetKind() == s_faceTypeName )
		{
			return true;
		}

		return false;
	}

	tools::EditorObjectID GeometryShapeEditMode::GetInsertLocation() const
	{
		const red::DynArray< EditorObjectID > selection = GetSelection();
		if ( selection.Size() == 1u )
		{
			return selection.Front();
		}
		return EditorObjectID();
	}

	Bool GeometryShapeEditMode::CanDeleteObjects( const red::ArraySpan<const EditorObjectIDPath>& paths, Bool isCut, String* outFailReason ) const
	{
		*outFailReason = "Unable to remove object in this edit mode, try dissolving them instead.";
		return false;
	}

	///
	/// Vertex Operations
	///


	EditorObjectID GeometryShapeEditMode::CreateVertexId( Uint64 id )
	{
		res::ResourcePath path = GetParentEditorObjectID().GetResourcePath();
		CName type = s_vertexTypeName;

		return EditorObjectID::Build( path, id != -1 ? id : m_vertexGeneratorId++, type );
	}

	EditorObjectID GeometryShapeEditMode::CreateVertex()
	{
		EditorObjectID id = CreateVertexId();
		CreateVertex( id );
		return id;
	}

	void GeometryShapeEditMode::CreateVertex( EditorObjectID id )
	{
		PreviewObjectPtr object = CreateHandle< GeometryVertexObject >( this, id );

		GeoVertex vertex;
		vertex.m_id = id;

		m_vertices.Insert( id, vertex );
		m_previewObjects.PushBack( object );

		MarkModified();
	}


	void GeometryShapeEditMode::DissolveVertex( const EditorObjectID& id, red::Set<EditorObjectID>& outRemovedObjects )
	{
		red::DynArray< EditorObjectID > affectedEdges{ red::PoolBackend() };
		for ( const auto& iter : m_edges )
		{
			const GeoEdge& edge = iter.Value();
			if ( edge.m_vertexEnd == id || edge.m_vertexStart == id )
			{
				affectedEdges.PushBack( edge.m_id );
			}
		}

		if ( affectedEdges.Size() == 2u )
		{
			for ( auto& face : m_faces )
			{
				if ( face.Value().m_vertices.Size() < 4u && face.Value().m_vertices.Exist( id ) )
				{
					RED_REPORT_ERROR( RED_LOCATION( GetParentEditorObjectID().GetResourcePath() ), "Unable to remove this vertex. Leads to invalid face." );
					return;
				}
			}

			const GeoEdge& edgeOne = GetEdge( affectedEdges[ 0 ] );
			const GeoEdge& edgeTwo = GetEdge( affectedEdges[ 1 ] );

			if ( edgeOne.m_vertexStart == id )
			{
				SetEdgeVertices( edgeOne.m_id, edgeTwo.m_vertexStart == id ? edgeTwo.m_vertexEnd : edgeTwo.m_vertexStart, edgeOne.m_vertexEnd );
			}
			else
			{
				SetEdgeVertices( edgeOne.m_id, edgeOne.m_vertexStart, edgeTwo.m_vertexStart == id ? edgeTwo.m_vertexEnd : edgeTwo.m_vertexStart );
			}


			Bool removeResult = false;
			for ( Uint32 i = 0u; i < m_previewObjects.Size(); ++i )
			{
				if ( m_previewObjects[ i ]->GetEditorObjectID() == edgeTwo.m_id )
				{
					outRemovedObjects.Insert( edgeTwo.m_id );
					m_previewObjects.RemoveAt( i );
					m_edges.Remove( edgeTwo.m_id );
					removeResult = true;
					break;
				}
			}
			RED_ASSERT( removeResult, "Failed to remove edge from dissolved vertex. Mesh is invalid now." );

			removeResult = false;
			for ( Uint32 i = 0u; i < m_previewObjects.Size(); ++i )
			{
				if ( m_previewObjects[ i ]->GetEditorObjectID() == id )
				{
					outRemovedObjects.Insert( id );
					m_previewObjects.RemoveAt( i );
					m_vertices.Remove( id );
					removeResult = true;
					break;
				}
			}
			RED_ASSERT( removeResult, "Failed to remove dissolved vertex. Mesh is invalid now." );
			MarkModified();

			Uint32 changedFaces = 0u;
			for ( auto& face : m_faces )
			{
				if ( face.Value().m_vertices.Remove( id ).IsSuccessful() )
				{
					++changedFaces;
				}
			}
			RED_ASSERT( changedFaces == 2u, "Dissolving vertex should modify excatly 2 faces" );

			for ( auto& previewObject : m_previewObjects )
			{
				if ( previewObject->GetEditorObjectID().GetKind() == s_faceTypeName )
				{
					Cast< GeometryFaceObject >( previewObject )->Invalidate();
				}
			}
		}
		else
		{
			RED_REPORT_ERROR( RED_LOCATION( GetParentEditorObjectID().GetResourcePath() ), "Unable to remove this vertex. Try removing the edge instead." );
		}
	}

	const GeoVertex& GeometryShapeEditMode::GetVertex( const EditorObjectID& vertexId ) const
	{
		return m_vertices[ vertexId ];
	}

	const Vector3& GeometryShapeEditMode::GetVertexPosition( const EditorObjectID& vertexId ) const
	{
		return GetVertex( vertexId ).m_position;
	}

	void GeometryShapeEditMode::SetVertexPosition( const EditorObjectID& vertexId, const Vector3& postion )
	{
		m_vertices[ vertexId ].m_position = postion;

		InvalidateCachedPosition( vertexId );

		MarkModified();
	}

	void GeometryShapeEditMode::GetVertexEdgesAndFaces( const EditorObjectID& id, red::DynArray< EditorObjectID >& edges, red::DynArray< EditorObjectID >& faces ) const
	{
		if ( id.GetKind() != s_vertexTypeName )
		{
			return;
		}

		edges.Clear();
		faces.Clear();

		// Brute force
		for ( const auto& iter : m_edges )
		{
			const GeoEdge& edge = iter.Value();

			if ( edge.m_vertexStart == id || edge.m_vertexEnd == id )
			{
				edges.PushBack( edge.m_id );
			}
		}

		for ( const auto& iter : m_faces )
		{
			const GeoFace& face = iter.Value();

			if ( face.m_vertices.Exist( id ) )
			{
				faces.PushBack( face.m_id );
			}
		}
	}

	void GeometryShapeEditMode::InvalidateCachedPosition( const EditorObjectID& id )
	{
		red::DynArray< EditorObjectID > affectedEdges;
		red::DynArray< EditorObjectID > affectedFaces;

		GetVertexEdgesAndFaces( id, affectedEdges, affectedFaces );

		for ( const EditorObjectID& id : affectedEdges )
		{
			UpdateEdgePostion( id );
		}

		for ( const EditorObjectID& id : affectedFaces )
		{
			UpdateFacePosition( id );
		}
	}

	///
	/// Edge operations
	///

	EditorObjectID GeometryShapeEditMode::CreateEdgeId( Uint64 id )
	{
		res::ResourcePath path = GetParentEditorObjectID().GetResourcePath();
		CName type = s_edgeTypeName;

		return EditorObjectID::Build( path, id != -1 ? id : m_edgeGeneratorId++, type );
	}

	EditorObjectID GeometryShapeEditMode::CreateEdge()
	{
		EditorObjectID id = CreateEdgeId();
		CreateEdge( id );
		return id;
	}

	void GeometryShapeEditMode::CreateEdge( EditorObjectID id )
	{
		PreviewObjectPtr object = CreateHandle< GeometryEdgeObject >( this, id );

		GeoEdge edge;
		edge.m_id = id;

		m_edges.Insert( id, edge );
		m_previewObjects.PushBack( object );

		MarkModified();
	}

	EditorObjectID GeometryShapeEditMode::CreateEdge( const EditorObjectID& vertex1, const EditorObjectID& vertex2 )
	{
		EditorObjectID newEdgeId = CreateEdge();
		SetEdgeVertices( newEdgeId, vertex1, vertex2 );
		return newEdgeId;
	}

	EditorObjectID GeometryShapeEditMode::FindVerticesFace( const EditorObjectID& vertex, const EditorObjectID& other, Bool& outIsOneEdge ) const
	{
		for ( const auto& face : m_faces )
		{
			const auto& vertices = face.Value().m_vertices;
			Int32 index = vertices.GetIndex( vertex );
			Int32 otherIndex = vertices.GetIndex( other );
			if ( index != red::INVALID_INDEX && otherIndex != red::INVALID_INDEX )
			{
				outIsOneEdge = Abs( index - otherIndex ) == 1 || Abs( index - otherIndex ) == vertices.Size() - 1;
				return face.Key();
			}
		}
		return EditorObjectID();
	}

	red::DynArray< EditorObjectID > GeometryShapeEditMode::FindFacesSharingEdge( const EditorObjectID& edgeId )
	{
		const auto& edge = GetEdge( edgeId );
		red::DynArray< EditorObjectID > faces{ red::PoolBackend() };
		for ( const auto& face : m_faces )
		{
			if ( face.Value().m_vertices.Exist( edge.m_vertexEnd ) && face.Value().m_vertices.Exist( edge.m_vertexStart ) )
			{
				faces.PushBack( face.Key() );
			}
		}
		RED_ASSERT( faces.Size() == 2u, "Logic problem - edge is shared by not excatly two faces" );
		return faces;
	}

	Bool GeometryShapeEditMode::MergeFaces( const EditorObjectID& faceOne, const EditorObjectID& faceTwo, red::Set< EditorObjectID >& outRemovedObjects )
	{
		red::DynArray< EditorObjectID > faceOneVertices{ red::PoolBackend() };
		red::DynArray< EditorObjectID > faceTwoVertices{ red::PoolBackend() };

		{
			red::DynArray< GeoVertex > faceOneVerticesFull{ red::PoolBackend() };
			GetFaceOrderedVertices( faceOne, faceOneVerticesFull );

			faceOneVertices.Resize( faceOneVerticesFull.Size() );
			std::transform( faceOneVerticesFull.Begin(), faceOneVerticesFull.End(), faceOneVertices.Begin(), [] ( const GeoVertex& vertex )
			{
				return vertex.m_id;
			} );
		}

		{
			red::DynArray< GeoVertex > faceTwoVerticesFull{ red::PoolBackend() };
			GetFaceOrderedVertices( faceTwo, faceTwoVerticesFull );

			faceTwoVertices.Resize( faceTwoVerticesFull.Size() );
			std::transform( faceTwoVerticesFull.Begin(), faceTwoVerticesFull.End(), faceTwoVertices.Begin(), [] ( const GeoVertex& vertex )
			{
				return vertex.m_id;
			} );
		}

		// Find any unshared vertex to start with
		Uint32 startingIndex = red::INVALID_INDEX;
		for ( Uint32 i = 0u; i < faceOneVertices.Size(); ++i )
		{
			if ( !faceTwoVertices.Exist( faceOneVertices[ i ] ) )
			{
				startingIndex = i;
				break;
			}
		}

		RED_ASSERT( startingIndex != red::INVALID_INDEX, "Unable to merge polygons, all edges are common? " );

		Uint32 i = startingIndex;
		red::DynArray< EditorObjectID > newFace{ red::PoolBackend() };
		newFace.Reserve( faceOneVertices.Size() + faceTwoVertices.Size() - 2 ); // Biggest and most likely case, one shared edge

		// Polygon Union
		// Verify. This could potentially lead to infinite loop if there are any bugs here. But it shouldn't. 
		// Assuming both faces have at least one common edge
		while ( true )
		{
			i = ( i + 1 ) % faceOneVertices.Size();
			newFace.PushBack( faceOneVertices[ i ] );

			if ( i == startingIndex )
			{
				break;
			}

			Int32 otherI = faceTwoVertices.GetIndex( faceOneVertices[ i ] );
			if ( otherI != red::INVALID_INDEX )
			{
				do
				{
					otherI = ( otherI + 1 ) % faceTwoVertices.Size();
					newFace.PushBack( faceTwoVertices[ otherI ] );

					i = faceOneVertices.GetIndex( faceTwoVertices[ otherI ] );
				} while ( i == red::INVALID_INDEX );
			}
		}

		red::Set< EditorObjectID > oldVertices{ red::PoolBackend() };
		red::Set< EditorObjectID > newVertices{ red::PoolBackend() };

		oldVertices.Reserve( faceOneVertices.Size() + faceTwoVertices.Size() );
		for ( const auto& vertex : faceTwoVertices )
		{
			oldVertices.Insert( vertex );
		}

		for ( const auto& vertex : faceOneVertices )
		{
			oldVertices.Insert( vertex );
		}

		newVertices.Reserve( newFace.Size() );
		for ( const auto& vertex : newFace )
		{
			newVertices.Insert( vertex );
		}

		if ( newVertices.Size() < newFace.Size() )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetParentEditorObjectID().GetResourcePath() ), "Created shape would be invalid" );
			return false;
		}

		oldVertices.Difference( newVertices );
		outRemovedObjects.Union( oldVertices );

		for ( const auto& edge : m_edges )
		{
			if ( oldVertices.Exist( edge.Value().m_vertexStart ) || oldVertices.Exist( edge.Value().m_vertexEnd ) )
			{
				outRemovedObjects.Insert( edge.Key() );
			}
		}

		SetFaceVertices( faceOne, newFace );
		RemoveFace( faceTwo );
		for ( const auto& vertex : oldVertices )
		{
			RemoveVertex( vertex );
		}
		return true;
	}

	void GeometryShapeEditMode::DissolveEdge( const EditorObjectID& id, red::Set<EditorObjectID>& outRemovedObjects )
	{
		red::DynArray< EditorObjectID > faces = FindFacesSharingEdge( id );

		red::Set< EditorObjectID > removedObjects{ red::PoolBackend() };
		if ( !MergeFaces( faces[ 0 ], faces[ 1 ], removedObjects ) )
			return;

		removedObjects.Insert( id );

		for ( Uint32 i = 0; i < m_previewObjects.Size(); ++i )
		{
			const EditorObjectID& id = m_previewObjects[ i ]->GetEditorObjectID();
			if ( id.GetKind() == s_edgeTypeName && removedObjects.Exist( id ) )
			{
				m_edges.Remove( m_previewObjects[ i ]->GetEditorObjectID() );
				m_previewObjects.RemoveAt( i );
				--i;

				MarkModified();
			}
		}

		outRemovedObjects.Union( removedObjects );
	}

	void GeometryShapeEditMode::RemoveVertex( const EditorObjectID& id )
	{
		RED_ASSERT( id.GetKind() == s_vertexTypeName, "Invalid object type, vertex expected" );

		m_vertices.Remove( id );
		RemovePreviewObject( id );

		MarkModified();
	}

	void GeometryShapeEditMode::RemoveEdge( const EditorObjectID& id )
	{
		RED_ASSERT( id.GetKind() == s_edgeTypeName, "Invalid object type, edge expected" );

		m_edges.Remove( id );
		RemovePreviewObject( id );

		MarkModified();
	}

	void GeometryShapeEditMode::RemoveFace( const EditorObjectID& id )
	{
		RED_ASSERT( id.GetKind() == s_faceTypeName, "Invalid object type, face expected" );

		m_faces.Remove( id );
		RemovePreviewObject( id );

		MarkModified();
	}

	void GeometryShapeEditMode::RemovePreviewObject( const EditorObjectID& id )
	{
		for ( Uint32 i = 0; i < m_previewObjects.Size(); ++i )
		{
			if ( m_previewObjects[ i ]->GetEditorObjectID() == id )
			{
				m_previewObjects.RemoveAt( i );
				MarkModified();
				return;
			}
		}
	}

	const GeoEdge& GeometryShapeEditMode::GetEdge( const EditorObjectID& edgeId ) const
	{
		return m_edges[ edgeId ];
	}

	void GeometryShapeEditMode::UpdateEdgePostion( const EditorObjectID& edgeId )
	{
		GeoVertex start;
		GeoVertex end;

		GetEdgeVertices( edgeId, start, end );

		const Vector3& pointA = start.m_position;
		const Vector3& pointB = end.m_position;

		m_edges[ edgeId ].m_cachedPosition = pointA + ( pointB - pointA ) * 0.5f;

		MarkModified();
	}

	const Vector3& GeometryShapeEditMode::GetEdgePosition( const EditorObjectID& edgeId ) const
	{
		return m_edges[ edgeId ].m_cachedPosition;
	}

	void GeometryShapeEditMode::SetEdgePosition( const EditorObjectID& edgeId, const Vector3& position )
	{
		GeoVertex start;
		GeoVertex end;

		GetEdgeVertices( edgeId, start, end );

		const Vector3& pointA = start.m_position;
		const Vector3& pointB = end.m_position;

		Vector3 old = m_edges[ edgeId ].m_cachedPosition;
		Vector3 diff = position - old;

		m_edges[ edgeId ].m_cachedPosition = position;

		SetVertexPosition( start.m_id, pointA + diff );
		SetVertexPosition( end.m_id, pointB + diff );

		MarkModified();
	}

	void GeometryShapeEditMode::GetEdgeVertices( const EditorObjectID& edgeId, GeoVertex& start, GeoVertex& end ) const
	{
		const GeoEdge& edge = GetEdge( edgeId );

		start = GetVertex( edge.m_vertexStart );
		end = GetVertex( edge.m_vertexEnd );
	}

	void GeometryShapeEditMode::SetEdgeVertices( const EditorObjectID& edgeId, const EditorObjectID& newStartVertexId, const EditorObjectID& newEndVertexId )
	{
		m_edges[ edgeId ].m_vertexStart = newStartVertexId;
		m_edges[ edgeId ].m_vertexEnd = newEndVertexId;

		UpdateEdgePostion( edgeId );
	}

	void GeometryShapeEditMode::GetFaceOrderedVertices( EditorObjectID faceId, red::DynArray< GeoVertex >& vertices ) const
	{
		const GeoFace& face = GetFace( faceId );
		for ( const auto& vertex : face.m_vertices )
		{
			vertices.PushBack( GetVertex( vertex ) );
		}
	}

	void GeometryShapeEditMode::SetFaceVertices( const EditorObjectID& faceId, const red::DynArray< EditorObjectID >& vertexIds )
	{
		m_faces[ faceId ].m_vertices = vertexIds;
		UpdateFacePosition( faceId );

		MarkModified();
	}

	EditorObjectID GeometryShapeEditMode::FindEdge( const EditorObjectID& vertexStart, const EditorObjectID& vertexEnd )
	{
		for ( const auto& edge : m_edges )
		{
			if ( edge.Value().m_vertexStart == vertexStart && edge.Value().m_vertexEnd == vertexEnd )
			{
				return edge.Key();
			}

			if ( edge.Value().m_vertexEnd == vertexStart && edge.Value().m_vertexStart == vertexEnd )
			{
				return edge.Key();
			}
		}
		RED_ASSERT( false, "Such edge doesn't exist" );
		return EditorObjectID();
	}

	void GeometryShapeEditMode::GetEdgeData( const EditorObjectID& edgeId, Vector3& direction, Float& length, Vector3& startPoint, Vector3& endPoint ) const
	{
		GeoVertex startVertex;
		GeoVertex endVertex;

		GetEdgeVertices( edgeId, startVertex, endVertex );

		Vector3 vector = endVertex.m_position - startVertex.m_position;

		direction = vector; 
		length = direction.Normalize();

		startPoint = startVertex.m_position;
		endPoint = endVertex.m_position;
	}

	void GeometryShapeEditMode::EdgeSubdivide( const EditorObjectID& edgeId, Float cutPosition, EditorObjectID& newVertexId, EditorObjectID& newEdgeId )
	{
		GeoVertex startVertex;
		GeoVertex endVertex;

		GetEdgeVertices( edgeId, startVertex, endVertex );

		Vector3 vector = endVertex.m_position - startVertex.m_position;
		Vector3 normalized = vector.Normalized();
		Float length = vector.Normalize();

		newVertexId = CreateVertex();
		newEdgeId = CreateEdge();

		SetEdgeVertices( edgeId, startVertex.m_id, newVertexId );
		SetEdgeVertices( newEdgeId, newVertexId, endVertex.m_id );

		for ( auto& face : m_faces )
		{
			const auto& vertices = face.Value().m_vertices;
			Int32 startVertexIdIndex = vertices.GetIndex( startVertex.m_id );
			if ( startVertexIdIndex != red::INVALID_INDEX )
			{
				Uint32 nextVertexIndex = ( startVertexIdIndex + 1 ) % vertices.Size();
				Uint32 prevVertexIndex = startVertexIdIndex == 0 ? vertices.Size() - 1 : startVertexIdIndex - 1;
				if ( vertices[ nextVertexIndex ] == endVertex.m_id )
				{
					auto verticesCopy = face.Value().m_vertices;
					verticesCopy.InsertAt( nextVertexIndex, newVertexId );
					SetFaceVertices( face.Key(), verticesCopy );
				}
				else if ( vertices[ prevVertexIndex ] == endVertex.m_id )
				{
					auto verticesCopy = face.Value().m_vertices;
					verticesCopy.InsertAt( startVertexIdIndex, newVertexId );
					SetFaceVertices( face.Key(), verticesCopy );
				}
			}
		}
		SetVertexPosition( newVertexId, startVertex.m_position + normalized * ( length * cutPosition ) );
	}

	const tools::GeoFace& GeometryShapeEditMode::GetFace( const EditorObjectID& faceId ) const
	{
		return m_faces[ faceId ];
	}

	///
	/// Face functions
	///

	EditorObjectID GeometryShapeEditMode::CreateFaceId( Uint64 id )
	{
		res::ResourcePath path = GetParentEditorObjectID().GetResourcePath();
		CName type = s_faceTypeName;

		return EditorObjectID::Build( path, id != -1 ? id : m_faceGeneratorId++, type );
	}

	EditorObjectID GeometryShapeEditMode::CreateFace()
	{
		EditorObjectID id = CreateFaceId();
		CreateFace( id );
		return id;
	}

	void GeometryShapeEditMode::CreateFace( EditorObjectID id )
	{
		PreviewObjectPtr object = CreateHandle< GeometryFaceObject >( this, id );

		GeoFace face;
		face.m_id = id;

		m_faces.Insert( id, face );
		m_previewObjects.PushBack( object );

		MarkModified();
	}

	const red::DynArray< EditorObjectID >& GeometryShapeEditMode::GetFaceVertices( EditorObjectID faceId ) const
	{
		return GetFace( faceId ).m_vertices;
	}

	Vector3 GeometryShapeEditMode::GetFaceNormal( const EditorObjectID& id ) const
	{
		Vector3 forward;
		Vector3 right;
		Vector3 normal;

		GetFaceVectors( id, forward, right, normal );

		return normal;
	}

	const Vector3& GeometryShapeEditMode::GetFacePosition( const EditorObjectID& id ) const
	{
		return m_faces[ id ].m_cachedPosition;
	}

	void GeometryShapeEditMode::GetFaceVectors( const EditorObjectID& id, Vector3& forward, Vector3& right, Vector3& normal ) const
	{
		const red::DynArray< EditorObjectID >& vertices = GetFaceVertices( id );

		const Vector3& v1 = GetVertexPosition( vertices[ 0 ] );
		const Vector3& v2 = GetVertexPosition( vertices[ 1 ] );
		const Vector3& v3 = GetVertexPosition( vertices[ 2 ] );

		forward = ( v2 - v1 ).Normalized();
		right = ( v2 - v3 ).Normalized();

		normal = forward.Cross( right ).Normalized();
	}

	void GeometryShapeEditMode::SetFacePosition( const EditorObjectID& id, const Vector3& position )
	{
		Vector3 oldPosition = GetFacePosition( id );
		Vector3 diff = position - oldPosition;

		const red::DynArray< EditorObjectID >& vertices = GetFaceVertices( id );

		for ( const EditorObjectID& id : vertices )
		{
			oldPosition = GetVertexPosition( id );
			SetVertexPosition( id, oldPosition + diff );
		}
	}

	void GeometryShapeEditMode::UpdateFacePosition( const EditorObjectID& id )
	{
		const red::DynArray< EditorObjectID >& vertices = GetFaceVertices( id );

		Vector3 sum;
		for ( const EditorObjectID& id : vertices )
		{
			Vector3 position = GetVertexPosition( id );
			sum += position;
		}

		m_faces[ id ].m_cachedPosition = sum / (Float)vertices.Size();
		MarkModified();

		for ( const PreviewObjectPtr& object : m_previewObjects )
		{
			if ( object->GetEditorObjectID() == id )
			{
				THandle< GeometryFaceObject > face = Cast< GeometryFaceObject >( object );
				face->Invalidate();
			}
		}
	}

	///
	/// Misc
	///

	void GeometryShapeEditMode::SubdivideSelectedEdges()
	{
		const red::DynArray< EditorObjectID >& selection = GetSelection();

		red::DynArray< EditorObjectID > edges;
		for ( const EditorObjectID& item : selection )
		{
			if ( item.GetKind() == s_edgeTypeName )
			{
				edges.PushBack( item );
			}
		}

		for ( const EditorObjectID& edge : edges )
		{
			EditorObjectID newVertexId;
			EditorObjectID newEdgeId;

			EdgeSubdivide( edge, 0.5f, newVertexId, newEdgeId );
		}
	}

	Bool GeometryShapeEditMode::SubdivideSelectedEdgesAndConnectVertices()
	{
		const red::DynArray< EditorObjectID >& selection = GetSelection();

		red::DynArray< EditorObjectID > edges;
		for ( const EditorObjectID& item : selection )
		{
			if ( item.GetKind() == s_edgeTypeName )
			{
				edges.PushBack( item );
			}
		}

		if ( edges.Size() != 2 )
		{
			return false;
		}
		
		red::DynArray< EditorObjectID > edgeFaces1 = FindFacesSharingEdge( edges[ 0 ] );
		red::DynArray< EditorObjectID > edgeFaces2 = FindFacesSharingEdge( edges[ 1 ] );

		EditorObjectID commonFace;
		Bool found = false;

		for ( const EditorObjectID& edgeFace1Id : edgeFaces1 )
		{
			for ( const EditorObjectID& edgeFace2Id : edgeFaces2 )
			{
				if ( edgeFace1Id == edgeFace2Id )
				{
					commonFace = edgeFace1Id;
					found = true;
					break;
				}
			}

			if ( found )
			{
				break;
			}
		}

		// Not sheering a face
		if ( !found )
		{
			return false;
		}

		red::SharedPtr< IPreviewCommand > command = red::CreateSharedPtr< EdgeSubdivideInputHandler >( this, edges[ 0 ], edges[ 1 ] );
		SetCommand( command );

		return true;
	}

	Bool GeometryShapeEditMode::ConnectVerticesByEdge( const EditorObjectID& newStartVertexId, const EditorObjectID& newEndVertexId, EditorObjectID& newEdgeId )
	{
		Bool isOneEdge = false;
		EditorObjectID faceId = FindVerticesFace( newStartVertexId, newEndVertexId, isOneEdge );
		if ( !faceId.IsValid() || isOneEdge )
		{
			return false;
		}

		newEdgeId = CreateEdge( newStartVertexId, newEndVertexId );
		DivideFace( faceId, newEdgeId );

		return true;
	}

	Bool GeometryShapeEditMode::ConnectSelectedVerticesByEdge()
	{
		const red::DynArray< EditorObjectID >& selection = GetSelection();

		red::DynArray< EditorObjectID > vertices;
		for ( const EditorObjectID& item : selection )
		{
			if ( item.GetKind() == s_vertexTypeName )
			{
				vertices.PushBack( item );
			}
		}

		if ( vertices.Size() != 2 )
		{
			return false;
		}

		EditorObjectID newEdgeid;
		return ConnectVerticesByEdge( vertices[ 0 ], vertices[ 1 ], newEdgeid );
	}

	// Turns face into two smaller ones, separated by newEdge. Adds them to 
	void GeometryShapeEditMode::DivideFace( EditorObjectID face, EditorObjectID newEdgeId )
	{
		const auto& oldFace = GetFace( face );
		const auto& newEdge = GetEdge( newEdgeId );

		red::DynArray< EditorObjectID > newFaceAVertices{ red::PoolBackend() };
		red::DynArray< EditorObjectID > newFaceBVertices{ red::PoolBackend() };

		Bool AFace = true;
		for ( const auto& vertex : oldFace.m_vertices )
		{
			if ( AFace )
			{
				newFaceAVertices.PushBack( vertex );
			}
			else
			{
				newFaceBVertices.PushBack( vertex );
			}

			if ( vertex == newEdge.m_vertexStart || vertex == newEdge.m_vertexEnd )
			{
				AFace = !AFace;
				if ( AFace )
				{
					newFaceAVertices.PushBack( vertex );
				}
				else
				{
					newFaceBVertices.PushBack( vertex );
				}
			}
		}

		SetFaceVertices( face, newFaceAVertices );
		auto faceB = CreateFace();
		SetFaceVertices( faceB, newFaceBVertices );
	}

	Bool GeometryShapeEditMode::ExtrudeFace()
	{
		const red::DynArray< EditorObjectID >& selection = GetSelection();

		red::DynArray< EditorObjectID > faces;
		for ( const EditorObjectID& item : selection )
		{
			if ( item.GetKind() == s_faceTypeName )
			{
				faces.PushBack( item );
			}
		}

		if ( faces.Size() != 1 )
		{
			return false;
		}

		EditorObjectID extrudedFace = ExtrudeFace( faces[ 0 ] );
		red::SharedPtr< IPreviewCommand > extrude = red::CreateSharedPtr< ExtrudeInputHandler >( this, extrudedFace );

		red::DynArray< EditorObjectID > newSelection;
		newSelection.PushBack( extrudedFace );

		SelectObjects( newSelection );
		SetCommand( extrude );

		return true;
	}

	EditorObjectID GeometryShapeEditMode::DuplicateVertex( const EditorObjectID& id )
	{
		Vector3 position = GetVertexPosition( id );

		EditorObjectID newVertexId = CreateVertex();
		SetVertexPosition( newVertexId, position );

		return newVertexId;
	}

	void GeometryShapeEditMode::DuplicateEdge( const EditorObjectID& id, EditorObjectID& newEdge, EditorObjectID& newVertex1, EditorObjectID& newVertex2 )
	{
		GeoVertex vertex1;
		GeoVertex vertex2;

		GetEdgeVertices( id, vertex1, vertex2 );

		newVertex1 = CreateVertex();
		newVertex2 = CreateVertex();
		newEdge = CreateEdge();

		SetVertexPosition( newVertex1, vertex1.m_position );
		SetVertexPosition( newVertex1, vertex2.m_position );

		SetEdgeVertices( newEdge, newVertex1, newVertex2 );
	}

	EditorObjectID GeometryShapeEditMode::ExtrudeFace( const EditorObjectID& id )
	{
		// Copy, due to we will remove the face
		red::DynArray< EditorObjectID > vertices = GetFaceVertices( id );
		// Remove extruded face
		RemoveFace( id );

		// Create extruded face vertices...
		red::DynArray< EditorObjectID > copiedVertices;
		for ( const EditorObjectID& vertex : vertices )
		{
			copiedVertices.PushBack( DuplicateVertex( vertex ) );
		}

		// Create edges of extruded face
		for ( Uint32 i = 0; i < copiedVertices.Size(); ++i )
		{
			Uint32 index1 = i;
			Uint32 index2 = i + 1 < copiedVertices.Size() ? i + 1 : 0;

			EditorObjectID startVertex = copiedVertices[ index1 ];
			EditorObjectID endVertex = copiedVertices[ index2 ];

			CreateEdge( startVertex, endVertex );
		}

		EditorObjectID extrudedFace = CreateFace();
		SetFaceVertices( extrudedFace, copiedVertices );

		// Create side faces of extruded face
		for ( Uint32 i = 0; i < copiedVertices.Size(); ++i )
		{
			Uint32 index1 = i;
			Uint32 index2 = i + 1 < copiedVertices.Size() ? i + 1 : 0;

			CreateEdge( vertices[ index1 ], copiedVertices[ i ] );

			red::DynArray< EditorObjectID > sideFaceVertices;
			sideFaceVertices.PushBack( vertices[ index1 ] );
			sideFaceVertices.PushBack( vertices[ index2 ] );
			sideFaceVertices.PushBack( copiedVertices[ index2 ] );
			sideFaceVertices.PushBack( copiedVertices[ index1 ] );

			EditorObjectID sideFace = CreateFace();
			SetFaceVertices( sideFace, sideFaceVertices );
		}

		return extrudedFace;
	}

	void GeometryShapeEditMode::RegisterShortcuts( ShortcutManager* manager, PreviewPanel* panel )
	{
		//TODO: If memory usage becomes a problem, prefer specialized actions instead of generic one serializing everything
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_1 ).Modifier( EInputKey::IK_LControl );
			ShortcutCallback callback = [ this ] ( const KeyEventData& )
			{
				SetSelectionMode( GeoSelectionMode::All );
				return true;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "isMeshEditorAllSelectionMode" ).Binding( binding ).Callback( callback ).Name( "All Selection mode" ).Context( "helperObjects" );
			manager->Register( shortcut );
		}
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_2 ).Modifier( EInputKey::IK_LControl );
			ShortcutCallback callback = [ this ] ( const KeyEventData& )
			{
				SetSelectionMode( GeoSelectionMode::Vertices );
				return true;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "isMeshEditorVertexSelectionMode" ).Binding( binding ).Callback( callback ).Name( "Vertex Selection mode" ).Context( "helperObjects" );
			manager->Register( shortcut );
		}
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_3 ).Modifier( EInputKey::IK_LControl );
			ShortcutCallback callback = [ this ] ( const KeyEventData& )
			{
				SetSelectionMode( GeoSelectionMode::Edges );
				return true;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "isMeshEditorEdgeSelectionMode" ).Binding( binding ).Callback( callback ).Name( "Edge Selection mode" ).Context( "helperObjects" );
			manager->Register( shortcut );
		}
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_4 ).Modifier( EInputKey::IK_LControl );
			ShortcutCallback callback = [ this ] ( const KeyEventData& )
			{
				SetSelectionMode( GeoSelectionMode::Faces );
				return true;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "isMeshEditorFaceSelectionMode" ).Binding( binding ).Callback( callback ).Name( "Face Selection mode" ).Context( "helperObjects" );
			manager->Register( shortcut );
		}
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_K ).Modifier( EInputKey::IK_LControl );
			ShortcutCallback callback = [ this ] ( const KeyEventData& )
			{
				return GetPreviewPanelSharedData()->GetActionHistory().Execute( red::CreateUniquePtr< GenericGeometryShapeEditModeAction >( this->GetPreviewPanelSharedData(), "Subdivide Selected Edges", [ this ] ()
				{
					SubdivideSelectedEdges();
					ProcessChanges();
					return true;
				} ) );
				return true;
			};
			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "isMeshEditorEdgeSubdivide" ).Binding( binding ).Callback( callback ).Name( "Subdivide edge" ).Context( "helperObjects" );

			manager->Register( shortcut );
		}
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_L ).Modifier( EInputKey::IK_LControl );
			ShortcutCallback callback = [ this ] ( const KeyEventData& )
			{
				return GetPreviewPanelSharedData()->GetActionHistory().Execute( red::CreateUniquePtr< GenericGeometryShapeEditModeAction >( this->GetPreviewPanelSharedData(), "Connect Vertices", [ this ] ()
				{
					Bool result = ConnectSelectedVerticesByEdge();
					ProcessChanges();
					return result;
				} ) );
				return true;
			};
			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "isMeshEditorCreateEdgeBetweenVertices" ).Binding( binding ).Callback( callback ).Name( "Create edge between vertices" ).Context( "helperObjects" );

			manager->Register( shortcut );
		}
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_E ).Modifier( EInputKey::IK_LControl );
			ShortcutCallback callback = [ this ] ( const KeyEventData& )
			{
				return GetPreviewPanelSharedData()->GetActionHistory().Execute( red::CreateUniquePtr< GenericGeometryShapeEditModeAction >( this->GetPreviewPanelSharedData(), "Extrude Face", [ this ] ()
				{
					Bool result = ExtrudeFace();
					ProcessChanges();
					return result;
				} ) );
				return true;
			};
			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "isMeshEditorExtrudeFace" ).Binding( binding ).Callback( callback ).Name( "Extrude face" ).Context( "helperObjects" );

			manager->Register( shortcut );
		}
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_R ).Modifier( EInputKey::IK_LControl );
			ShortcutCallback callback = [ this ] ( const KeyEventData& )
			{
				return GetPreviewPanelSharedData()->GetActionHistory().Execute( red::CreateUniquePtr< GenericGeometryShapeEditModeAction >( this->GetPreviewPanelSharedData(), "Loop Cut", [ this ] ()
				{
					Bool result = LoopCut();
					ProcessChanges();
					return result;
				} ) );
			};
			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "isMeshEditorLoopCut" ).Binding( binding ).Callback( callback ).Name( "Loop Cut" ).Context( "helperObjects" );

			manager->Register( shortcut );
		}
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_D ).Modifier( EInputKey::IK_LControl );
			ShortcutCallback callback = [ this ] ( const KeyEventData& )
			{
				return GetPreviewPanelSharedData()->GetActionHistory().Execute( red::CreateUniquePtr< GenericGeometryShapeEditModeAction >( this->GetPreviewPanelSharedData(), "Dissolve Selected Object", [ this ] ()
				{
					Bool result = DissolveSelection();
					ProcessChanges();
					return result;
				} ) );
			};
			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "isMeshEditorDissolveSelection" ).Binding( binding ).Callback( callback ).Name( "Dissolve Selected Object" ).Context( "helperObjects" );

			manager->Register( shortcut );
		}
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_L ).Modifier( EInputKey::IK_Shift );
			ShortcutCallback callback = [ this ]( const KeyEventData& )
			{
				return GetPreviewPanelSharedData()->GetActionHistory().Execute( red::CreateUniquePtr< GenericGeometryShapeEditModeAction >( this->GetPreviewPanelSharedData(), "Subdivide selected edges and connect vertices", [ this ]()
				{
					Bool result = SubdivideSelectedEdgesAndConnectVertices();
					ProcessChanges();
					return result;
				} ) );
			};
			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "isMeshEditorSubdivideEdgesAndConnectVertex" ).Binding( binding ).Callback( callback ).Name( "Subdivide selected edges and connect vertices" ).Context( "helperObjects" );

			manager->Register( shortcut );
		}
	}

	void GeometryShapeEditMode::UnregisterShortcuts( ShortcutManager* manager )
	{
		manager->Unregister( "isMeshEditorAllSelectionMode" );
		manager->Unregister( "isMeshEditorVertexSelectionMode" );
		manager->Unregister( "isMeshEditorEdgeSelectionMode" );
		manager->Unregister( "isMeshEditorFaceSelectionMode" );
		manager->Unregister( "isMeshEditorEdgeSubdivide" );
		manager->Unregister( "isMeshEditorCreateEdgeBetweenVertices" );
		manager->Unregister( "isMeshEditorExtrudeFace" );
		manager->Unregister( "isMeshEditorLoopCut" );
		manager->Unregister( "isMeshEditorDissolveSelection" );
	}

	struct EdgeToCut
	{
		EditorObjectID edgeID;
		Float cutPosition;
	};

	struct VerticesFacePair
	{
		EditorObjectID firstVertex;
		EditorObjectID secondVertex;
		EditorObjectID face;
	};

	// TODO[PB]: Make loop cut not jump to the next shape if two are hit
	Bool GeometryShapeEditMode::LoopCut()
	{
		const red::DynArray< EditorObjectID >& selection = GetSelection();
		if ( selection.Size() != 1u )
		{
			return false;
		}

		if ( selection[ 0u ].GetKind() != s_edgeTypeName )
		{
			return false;
		}

		GeoVertex start, end;
		GetEdgeVertices( selection[ 0u ], start, end );

		// Todo[PB]: Cuts at the half of selected edge, perpendicular to it's direction, maybe use input handler instead?
		Plane cutPlane{ ( end.m_position - start.m_position ).Normalized(), ( end.m_position + start.m_position ) / 2.0f };

		red::DynArray< EdgeToCut > edgesToCut{ red::PoolBackend() };

		for ( const auto& edge : m_edges )
		{
			const Vector4 startPos = GetVertexPosition( edge.Value().m_vertexEnd );
			const Vector4 endPos = GetVertexPosition( edge.Value().m_vertexStart );
			Vector4 direction = endPos - startPos;
			Float edgeLength = direction.Mag3();
			direction /= edgeLength;

			Vector4 intersectionPosition;
			Float intersectionDistance;
			if ( cutPlane.IntersectLine( startPos, direction, intersectionPosition, intersectionDistance ) )
			{
				if ( intersectionDistance > 0.0f && intersectionDistance < edgeLength )
				{
					Float cutRatio = intersectionDistance / edgeLength;
					edgesToCut.PushBack( { edge.Key(), cutRatio } );
				}
			}
		}

		red::DynArray< EditorObjectID > newVertices{ red::PoolBackend() };
		newVertices.Reserve( edgesToCut.Size() );
		for ( const auto& cut : edgesToCut )
		{
			EditorObjectID newVertexId, newEdgeId;
			EdgeSubdivide( cut.edgeID, 1.0f - cut.cutPosition, newVertexId, newEdgeId );
			newVertices.PushBack( newVertexId );
		}

		red::DynArray< VerticesFacePair > sameFaceVertices{ red::PoolBackend() };
		red::Map< EditorObjectID, Uint32> numberOfNewEdgesPerFace{ red::PoolBackend() };
		for ( Uint32 i = 0u; i < newVertices.Size(); ++i )
		{
			for ( Uint32 j = i + 1; j < newVertices.Size(); ++j )
			{
				Bool isOneEdge = false;
				EditorObjectID faceId = FindVerticesFace( newVertices[ i ], newVertices[ j ], isOneEdge );
				if ( faceId.IsValid() && !isOneEdge )
				{
					sameFaceVertices.PushBack( { newVertices[ i ], newVertices[ j ], faceId } );
					if ( numberOfNewEdgesPerFace.KeyExist( faceId ) )
					{
						++numberOfNewEdgesPerFace[ faceId ];
					}
					else
					{
						numberOfNewEdgesPerFace[ faceId ] = 1u;
					}
				}
			}
		}

		red::DynArray< EditorObjectID > edgesToSelect{ red::PoolBackend() };
		for ( const auto& vertexPair : sameFaceVertices )
		{
			if ( numberOfNewEdgesPerFace[ vertexPair.face ] == 1u )
			{
				EditorObjectID newEdgeId = CreateEdge( vertexPair.firstVertex, vertexPair.secondVertex );
				DivideFace( vertexPair.face, newEdgeId );
				edgesToSelect.PushBack( newEdgeId );
			}
		}

		SelectObjects( edgesToSelect );
		return true;
	}

	Bool GeometryShapeEditMode::DissolveSelection()
	{
		const red::DynArray< EditorObjectID >& selection = GetSelection();
		red::Set< EditorObjectID > removedObjects{ red::PoolBackend() };

		for ( const EditorObjectID& id : selection )
		{
			if ( removedObjects.Exist( id ) )
			{
				continue;
			}

			if ( id.GetKind() == s_vertexTypeName )
			{
				DissolveVertex( id, removedObjects );
				continue;
			}

			if ( id.GetKind() == s_edgeTypeName )
			{
				DissolveEdge( id, removedObjects );
				continue;
			}
		}

		SelectObjects( {} );
		return true;
	}


	ObjectPersistentCollectionPtr GeometryShapeEditMode::CaptureAll()
	{
		red::DynArray< EditorObjectIDPath > objectToCapture{ red::PoolBackend() };
		objectToCapture.Reserve( m_edges.Size() + m_vertices.Size() + m_faces.Size() );

		for ( const auto& edge : m_edges )
		{
			objectToCapture.PushBack( EditorObjectIDPath{ edge.Key() } );
		}

		for ( const auto& vertex : m_vertices )
		{
			objectToCapture.PushBack( EditorObjectIDPath{ vertex.Key() } );
		}

		for ( const auto& face : m_faces )
		{
			objectToCapture.PushBack( EditorObjectIDPath{ face.Key() } );
		}

		return PreviewEditModeAdapter::CaptureObjects( objectToCapture );
	}

	Bool GeometryShapeEditMode::RestoreAll( const ObjectPersistentCollectionPtr& objects )
	{
		m_vertices.Clear();
		m_edges.Clear();
		m_faces.Clear();
		m_previewObjects.Clear();

		red::DynArray< EditorObjectIDPath > outActualCreatedPaths;
		return Action_RestoreObjects( objects, outActualCreatedPaths ); // Preview should be here?
	}

	void GeometryShapeEditMode::ProcessChanges()
	{
		if ( !m_hasPendingChanges )
		{
			return;
		}

		m_hasPendingChanges = false;
		UpdateGeometryShape();
	}

	///	
	///
	///

	GeometryVertexObject::GeometryVertexObject()
	{
	}

	GeometryVertexObject::GeometryVertexObject( PreviewEditModeAdapter* mode, EditorObjectID editorId )
		: PreviewObject( mode, editorId )
	{
	}

	GeometryVertexObject::~GeometryVertexObject() = default;

	GeometryShapeEditMode* GeometryVertexObject::GetMesh() const
	{
		return static_cast<GeometryShapeEditMode*>( GetEditMode() );
	}

	Vector3 GeometryVertexObject::GetPosition() const
	{
		return GetMesh()->GetVertexPosition( GetEditorObjectID() );
	}

	Bool GeometryVertexObject::Move( const Vector3& postion )
	{
		GetMesh()->SetVertexPosition( GetEditorObjectID(), postion );
		GetMesh()->ProcessChanges();
		return true;
	}

	void GeometryVertexObject::OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder, Bool overlay ) const
	{
		Bool isVertexSelectionMode = GetMesh()->IsSelectionModeEnabled( GeoSelectionMode::Vertices );
		if ( !isVertexSelectionMode )
		{
			return;
		}

		// calculate box size (point representation)
		const Vector3 pos = GetMesh()->GetVertexPosition( GetEditorObjectID() );
		const Vector3& scale = GetEditMode()->GetScale();
		const Vector3 transformedPos = GetEditMode()->GetTransform().TransformPoint( pos * scale );

		Float screenSpaceScale = viewport.CalcScreenSpaceScale( transformedPos );
		builder.SetColor( GetColor( overlay ) );

		Float controlPointSize = math::Max( 0.0f, screenSpaceScale * 0.04f );
		Vector3 extrude = Vector3( controlPointSize, controlPointSize, controlPointSize );

		builder.BindSelectable( this );
		builder.AddSolidBox( transformedPos - extrude, transformedPos + extrude );

		// debug vertex id draw
		/*
		String id = String::Printf( "v%d", GetEditorObjectID().GetInternalID() );
		builder.AddWorldText( transformedPos, 0, 0, id );
		//*/
	}

	///	
	///
	///

	GeometryEdgeObject::GeometryEdgeObject()
	{
	}

	GeometryEdgeObject::GeometryEdgeObject( PreviewEditModeAdapter* mode, EditorObjectID editorId )
		: PreviewObject( mode, editorId )
	{
	}

	GeometryEdgeObject::~GeometryEdgeObject() = default;

	GeometryShapeEditMode* GeometryEdgeObject::GetMesh() const
	{
		return static_cast<GeometryShapeEditMode*>( GetEditMode() );
	}

	Vector3 GeometryEdgeObject::GetPosition() const
	{
		return GetMesh()->GetEdgePosition( GetEditorObjectID() );
	}

	Bool GeometryEdgeObject::Move( const Vector3& position )
	{
		GetMesh()->SetEdgePosition( GetEditorObjectID(), position );
		GetMesh()->ProcessChanges();
		return true;
	}

	void GeometryEdgeObject::OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder, Bool overlay ) const
	{
		GeoVertex start;
		GeoVertex end;

		GetMesh()->GetEdgeVertices( GetEditorObjectID(), start, end );

		const Vector3& pointA = start.m_position;
		const Vector3& pointB = end.m_position;

		const Vector3& scale = GetMesh()->GetScale();
		const Transform& transform = GetMesh()->GetTransform();

		// calculate box size (point representation)
		const Vector3 vectorA = transform.TransformPoint( scale * pointA );
		const Vector3 vectorB = transform.TransformPoint( scale * pointB );

		Bool isEdgeSelectionMode = GetMesh()->IsSelectionModeEnabled( GeoSelectionMode::Edges );
		if ( isEdgeSelectionMode )
		{
			builder.SetColor( GetColor( overlay ) );
			builder.BindSelectable( this );
			builder.AddFatLine( vectorA, vectorB, 0.025f );
		}
		else
		{
			builder.BindSelectable( nullptr );
			builder.SetColor( Color::BLACK() );
			builder.AddLine( vectorA, vectorB );
		}

		// debug edges id draw
		/*
		String id = String::Printf( "e%d", GetEditorObjectID().GetInternalID() );
		builder.AddWorldText( ( vectorA + vectorB ) / 2.0f, 0, 0, id );
		//*/
	}

	///
	///
	///

	GeometryFaceObject::GeometryFaceObject()
	{
	}

	GeometryFaceObject::GeometryFaceObject( PreviewEditModeAdapter* mode, EditorObjectID editorId )
		: PreviewObject( mode, editorId )
	{
	}

	GeometryFaceObject::~GeometryFaceObject() = default;

	GeometryShapeEditMode* GeometryFaceObject::GetMesh() const
	{
		return static_cast<GeometryShapeEditMode*>( GetEditMode() );
	}

	Vector3 GeometryFaceObject::GetPosition() const
	{
		return GetMesh()->GetFacePosition( GetEditorObjectID() );
	}

	Bool GeometryFaceObject::Move( const Vector3& position )
	{
		GetMesh()->SetFacePosition( GetEditorObjectID(), position );
		GetMesh()->ProcessChanges();
		return true;
	}

	void GeometryFaceObject::OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder, Bool front ) const
	{
		IHelperObject::ColorMode colorMode = IHelperObject::ColorMode::Normal;
		builder.EnableTransparency( true );

		Bool isEdgeSelectionMode = GetMesh()->IsSelectionModeEnabled( GeoSelectionMode::Faces );
		builder.BindSelectable( isEdgeSelectionMode ? this : nullptr );

		Color color = GetColor( false );
		color.A = 160u;

		if ( front )
		{
			if ( !IsSelected() && !IsHovered() )
			{
				color = Color( 4, 93, 235, 96 );
			}
			builder.SetColor( color );
			builder.AddSolidIndexedTriangles( m_vertices.TypedData(), sizeof( Vector3 ), m_vertices.Size(), m_indices.TypedData(), m_indices.Size(), false );
		}
		else
		{
			if ( !IsSelected() && !IsHovered() )
			{
				color = Color( 235, 38, 4, 96 );
			}
			builder.SetColor( color );
			builder.AddSolidIndexedTriangles( m_vertices.TypedData(), sizeof( Vector3 ), m_vertices.Size(), m_indices.TypedData(), m_indices.Size(), true );
		}
	}

	void GeometryFaceObject::Invalidate()
	{
		red::DynArray< GeoVertex > vertices{ red::PoolBackend() };
		GetMesh()->GetFaceOrderedVertices( GetEditorObjectID(), vertices );

		const Vector3& scale = GetMesh()->GetScale();
		const Transform& transform = GetMesh()->GetTransform();

		m_vertices.Clear();
		m_vertices.Reserve( vertices.Size() );

		for ( const GeoVertex& vertex : vertices )
		{
			m_vertices.PushBack( transform.TransformPoint( scale * vertex.m_position ) );
		}

		m_indices.Clear();
		TriangulateVertices( m_vertices, m_indices );
	}

	Bool GeometryFaceObject::TriangulateVertices( const red::DynArray< Vector3 >& vertices, red::DynArray< Uint16 >& outTriangles ) const
	{
		if ( vertices.Size() < 3u )
		{
			return false;
		}

		ConvexPartitionPoly inputPoly;
		inputPoly.Init( vertices.Size() );

		Uint32 thirdPointIndex;
		const Vector3 firstEdgeNormalized = ( vertices[ 1u ] - vertices[ 0u ] ).Normalized();
		for ( thirdPointIndex = 2u; thirdPointIndex < vertices.Size(); ++thirdPointIndex )
		{
			const Float dotResult = firstEdgeNormalized.Cross( ( vertices[ thirdPointIndex ] - vertices[ 0u ] ).Normalized() ).SquareMag();
			if ( dotResult > 0.01f )
			{
				break;
			}
		}

		if ( thirdPointIndex == vertices.Size() )
		{
			// Unable to triangulate, are all points collinear?
			return false;
		}

		Plane plane( vertices[ 0u ], vertices[ 1u ], vertices[ thirdPointIndex ] );
		// Good enough :) 
		// If cause any problem, could use some points cloud -> plane fitting algorithm, but those algorithms look heavy

		// For easier calculations, assume plane origin is Vector3::ZEROS
		const Vector3 planeNormal = Vector3( plane.GetVectorRepresentation() ).Normalized();
		const Vector3 planeX = planeNormal.Cross( ( planeNormal.Abs() - Vector3::EX() ).IsAlmostZero( 0.1f ) ? Vector3::EY() : Vector3::EX() );
		const Vector3 planeY = planeNormal.Cross( planeX );

		for ( Uint32 i = 0; i < vertices.Size(); ++i )
		{
			inputPoly[ i ].mPoint = Vector2{ planeX.Dot( vertices[ i ] ), planeY.Dot( vertices[ i ] ) };
			inputPoly[ i ].mIndex = i;
		}

		Vector2 p0 = inputPoly[ 0u ].mPoint;
		Vector2 p1 = inputPoly[ 1u ].mPoint;

		const Int32 orientationCCW = 1;
		inputPoly.SetOrientation( orientationCCW );

		//////////////////////////////////////////////////////////////////////////

			// check if the shape has no crossing edges
		if ( !ConvexPartitionEngine::VerifyPolygon( &inputPoly ) )
		{
			// TBD[PB]: Can I do something about polygons that overlaps in 2D, but not in 3D?
			return false;
		}

		// decompose concave stuff into convex pieces
		ConvexPartitionPolyList outPolygons{ inkRend::PoolInkRendering_Math() };
		if ( !ConvexPartitionEngine::ConvexPartition( &inputPoly, &outPolygons ) )
		{
			return false;
		}

		// calculate triangles
		ConvexPartitionPolyList outConvexPolyTriangles{ inkRend::PoolInkRendering_Math() };
		if ( !ConvexPartitionEngine::Triangulate( &inputPoly, &outConvexPolyTriangles ) )
		{
			return false;
		}

		for ( Uint32 i = 0; i < outConvexPolyTriangles.Size(); ++i )
		{
			const ConvexPartitionPoly& triangle = outConvexPolyTriangles[ i ];

			outTriangles.PushBack( triangle.GetPoint( 0 ).mIndex );
			outTriangles.PushBack( triangle.GetPoint( 1 ).mIndex );
			outTriangles.PushBack( triangle.GetPoint( 2 ).mIndex );
		}

		return true;
	}

	Bool GeometryData::Edge::operator==( const Edge& other ) const
	{
		return ( m_vertexStart == other.m_vertexStart && m_vertexEnd == other.m_vertexEnd ) || ( m_vertexStart == other.m_vertexEnd && m_vertexEnd == other.m_vertexStart );
	}

}
