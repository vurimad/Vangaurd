/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "helperObjectVisConvexMeshEdge.h"
#include "helperObjectVisConvexMesh.h"
#include "previewEditMode.h"
#include "previewInput.h"
#include "selectionHandler.h"
#include "editActionHistory.h"
#include "previewInputHandler.h"
#include "../../common/core/include/configVar.h"
#include "../../common/core/include/editableProxy.h"
#include "../../common/core/include/editableView.h"

// TEMPORARY
#if 0

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( HelperObjectVisConvexMeshEdge, tools );
	RTTI_PARENT_TYPE( HelperObjectSubobject );
	RTTI_PROPERTY( m_faceIndex ).readOnly();
	RTTI_PROPERTY( m_edgeIndex ).readOnly();
RTTI_END_TYPE();


namespace Config
{
	TConfigVar< Float, Validation::FloatRange< 1, 100, 1000> > cvVisConvexMeshEdgeThickness( "Editor/Occlusion", "ConvexEdgeThickness", 0.01f );
} // config

namespace tools
{

	class VisConvexEdgeEditableProxy final : public HelperObjectEditableProxy
	{
	public:
		static const CName PROXY_TYPE;

	public:
		VisConvexEdgeEditableProxy( HelperObjectVisConvexMesh* meshObj, Uint16 faceIndex, Uint16 edgeIndex, EditorObjectID editorObjectID )
			: HelperObjectEditableProxy( editorObjectID )
			, m_meshObj( meshObj )
			, m_faceIndex( faceIndex )
			, m_edgeIndex( edgeIndex )
		{
		}

		virtual bool RestoreObject() const override
		{
			return EnsureExistance();
		}

		virtual void ToggleSelectionVisualization( const Bool isSelected ) const override
		{
			if ( EnsureExistance() )
			{
				m_edgeObj->m_isSelected = isSelected;
			}
		}

		virtual const ViewClass GetViewClass() const override
		{
			if ( !EnsureExistance() )
			{
				return CName::NONE();
			}

			return m_edgeObj->GetClass()->GetName();
		}

		virtual const ViewClass FindCompatibleViewClass( const ViewClass viewClass ) const override
		{
			if ( !EnsureExistance() )
			{
				return CName::NONE();
			}

			if ( m_edgeObj->GetClass()->GetName() != viewClass )
			{
				return CName::NONE();
			}

			return m_edgeObj->GetClass()->GetName();
		}

		virtual red::SharedPtr<IEditableView> CreateView( const ViewClass viewClass ) const override
		{
			if ( !EnsureExistance() )
			{
				return red::SharedPtr<IEditableView>();
			}

			return m_edgeObj->CreateView( nullptr, IsReadOnly(), viewClass );
		}

		virtual Matrix GetWorldTransform() const override
		{
			if ( !EnsureExistance() )
			{
				return Matrix::IDENTITY;
			}

			const Vector3 startPoint = m_edgeObj->GetStartPosition();
			const Vector3 endPoint = m_edgeObj->GetEndPosition();
			const Vector3 midPoint = startPoint*0.5f + endPoint*0.5f;
			const Vector3 transformedPoint = m_meshObj->GetTransform().TransformPoint( midPoint );

			Matrix matrix;
			const Vector3 axis = endPoint - startPoint;
			matrix.BuildFromDirectionVector( axis.Normalized() );
			matrix.SetTranslation( transformedPoint );
			return matrix;
		}

		virtual Bool SetWorldTransform( const Matrix& localToWorld ) override
		{
			if ( !EnsureExistance() )
			{
				return false;
			}

			// don't move points if face is also selected. face movement will handle it
			if ( m_meshObj->GetFaceSubobject( m_faceIndex )->m_isSelected )
			{
				return true;
			}

			Matrix mat = m_meshObj->GetInvertTransform();
			Vector3 transformedPoint = mat.TransformPoint( Vector3( localToWorld.GetRow(3) ) );

			// #TODO: Could maybe support rotation around the edge's axis or something... would only really work with a
			// local-space transform gizmo.

			// Ensure the vertices remain planar
			// #TODO: Might be nice if it were possible to change the face orientation by moving edges. For example, a
			// person could have a quad, select one edge, and translate that anywhere while still having a planar far.
			// Could work fine for a single edge, but breaks down if multiple vertices/edges/etc can be moved together.
			const auto& verts = m_meshObj->GetOccluderNode()->GetFaceVertices( m_edgeObj->m_faceIndex );
			if ( verts.Size() >= 3 )
			{
				// #TODO: Would be nice to better preserve the face plane. As-is, could end up forming some degenerate
				// intermediate step, and actually change the plane. Could just store the plane for each face outright.
				Plane plane(verts[0], verts[1], verts[2]);
				transformedPoint = plane.Project(transformedPoint);
			}

			const Vector3 startPoint = m_edgeObj->GetStartPosition();
			const Vector3 endPoint = m_edgeObj->GetEndPosition();
			const Vector3 midPoint = startPoint*0.5f + endPoint*0.5f;

			Vector3 offset = transformedPoint - midPoint;

			m_edgeObj->Move( offset );

			return true;
		}

		virtual Bool EnsureExistance() const override
		{
			m_edgeObj = m_meshObj->GetEdgeSubobject( m_faceIndex, m_edgeIndex );
			return m_edgeObj != nullptr;
		}

	private:
		mutable HelperObjectVisConvexMeshEdgePtr	m_edgeObj;

		HelperObjectVisConvexMesh*					m_meshObj;
		Uint16										m_faceIndex;
		Uint16										m_edgeIndex;
	};

	const CName VisConvexEdgeEditableProxy::PROXY_TYPE( "HelperObjectVisConvexMeshEdge" ); 


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	HelperObjectVisConvexMeshEdge::HelperObjectVisConvexMeshEdge( HelperObjectVisConvexMesh* meshObj, Uint16 faceIndex, Uint16 edgeIndex )
		: m_meshObj( meshObj )
		, m_faceIndex( faceIndex )
		, m_edgeIndex( edgeIndex )
		, m_isSelected( false )
	{
	}

	Vector3 HelperObjectVisConvexMeshEdge::GetStartPosition() const
	{
		const auto& verts = m_meshObj->GetOccluderNode()->GetFaceVertices( m_faceIndex );
		return verts[m_edgeIndex];
	}

	Vector3 HelperObjectVisConvexMeshEdge::GetEndPosition() const
	{
		const auto& verts = m_meshObj->GetOccluderNode()->GetFaceVertices( m_faceIndex );
		return verts[( m_edgeIndex + 1) % verts.Size()];
	}

	Uint16 HelperObjectVisConvexMeshEdge::GetStartVertexIndex() const
	{
		return m_edgeIndex;
	}

	Uint16 HelperObjectVisConvexMeshEdge::GetEndVertexIndex() const
	{
		const Uint32 numVerts = m_meshObj->GetOccluderNode()->GetFaceSize( m_faceIndex );
		return ( m_edgeIndex + 1 ) % numVerts;
	}


	void HelperObjectVisConvexMeshEdge::OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder ) const
	{
		const Bool isEdgeSelected = m_isSelected;
		const Bool isVertSelected = m_meshObj->GetVertexSubobject(m_faceIndex, GetStartVertexIndex())->m_isSelected || m_meshObj->GetVertexSubobject(m_faceIndex, GetEndVertexIndex())->m_isSelected;
		const Bool isFaceSelected = m_meshObj->GetFaceSubobject(m_faceIndex)->m_isSelected;

		builder.BindSelectable( this );
		builder.SetColor( HelperObjectVisConvexMesh::GetSubobjectColor( isEdgeSelected, isVertSelected||isFaceSelected ) );

		const auto& verts = m_meshObj->GetOccluderNode()->GetFaceVertices( m_faceIndex );

		Vector3 pointA = verts[m_edgeIndex];
		Vector3 pointB = verts[( m_edgeIndex + 1) % verts.Size()];

		pointA = m_meshObj->GetTransform().TransformPoint( pointA );
		pointB = m_meshObj->GetTransform().TransformPoint( pointB );


		// Render thick line by making some quads oriented to face the camera.
		Vector3 vectorX, vectorY;
		const Vector3 tangent = pointB - pointA;
		vectorX = pointB - viewport.m_camera.GetPosition();

		vectorY = tangent.Cross( vectorX ).Normalized();
		vectorX = vectorY.Cross( tangent ).Normalized();

		const Float scaleA = viewport.CalcScreenSpaceScale( pointA ) * Config::cvVisConvexMeshEdgeThickness.Get();
		const Float scaleB = viewport.CalcScreenSpaceScale( pointB ) * Config::cvVisConvexMeshEdgeThickness.Get();

		const Vector3 vertexBuffer[8] = {
			pointA + ( vectorX + vectorY) * scaleA,
			pointA + ( vectorX - vectorY) * scaleA,
			pointA + (-vectorX - vectorY) * scaleA,
			pointA + (-vectorX + vectorY) * scaleA,
			pointB + ( vectorX + vectorY) * scaleB,
			pointB + ( vectorX - vectorY) * scaleB,
			pointB + (-vectorX - vectorY) * scaleB,
			pointB + (-vectorX + vectorY) * scaleB,
		};

		const Uint16 indexBuffer[3*8] = {
			4, 0, 1,	4, 1, 5,
			5, 1, 2,	5, 2, 6,
			6, 2, 3,	6, 3, 7,
			7, 3, 0,	7, 0, 4,
		};

		// Thick edge drawn as overlay if it, its face, or a vertex is selected. Makes it easier to select an edge for a face.
		builder.EnableOverlay( isEdgeSelected || isFaceSelected || isVertSelected );

		builder.AddSolidIndexedTriangles( vertexBuffer, 8, indexBuffer, 3 * 8 );

		builder.EnableOverlay( true );
		builder.AddLine( pointA, pointB );
		builder.EnableOverlay( false );
	}

	EditableProxyPtr HelperObjectVisConvexMeshEdge::Selectable_CreateProxy() const
	{
		return EditableProxyPtr( new VisConvexEdgeEditableProxy( m_meshObj, m_faceIndex, m_edgeIndex, Selectable_CreateEditorId() ) );
	}

	tools::EditorObjectID HelperObjectVisConvexMeshEdge::Selectable_CreateEditorId() const
	{
		const Uint16 index_plusOne = m_edgeIndex + 1; // don't want 0 ids
		return EditorObjectID::Build( index_plusOne | (m_faceIndex<<16), VisConvexEdgeEditableProxy::PROXY_TYPE );
	}

	void HelperObjectVisConvexMeshEdge::Move( const Vector3& offset )
	{
		// #TODO: vertex movement notification should be send after LMB is released, so occluder is not recalculated
		// every mouse move. or at the very least, once per move instead of for each selected edge

		auto node = m_meshObj->GetOccluderNode();
		node->MoveVertex( m_faceIndex, GetStartVertexIndex(), offset, true );
		node->MoveVertex( m_faceIndex, GetEndVertexIndex(), offset, true );
	}

}

#endif