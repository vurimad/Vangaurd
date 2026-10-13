/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "helperObjectVisConvexMeshVertex.h"
#include "helperObjectVisConvexMesh.h"
#include "previewEditMode.h"
#include "previewInput.h"
#include "selectionHandler.h"
#include "previewInputHandler.h"
#include "../../common/core/include/configVar.h"
#include "../../common/core/include/editableProxy.h"
#include "../../common/core/include/editableView.h"

// TEMPORARY
#if 0

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( HelperObjectVisConvexMeshVertex, tools );
	RTTI_PARENT_TYPE( HelperObjectSubobject );
	RTTI_PROPERTY( m_faceIndex ).readOnly();
	RTTI_PROPERTY( m_vertIndex ).readOnly();
RTTI_END_TYPE();


namespace Config
{
	TConfigVar< Float, Validation::FloatRange< 1, 10, 100> > cvVisConvexShapeVertexSize( "Editor/Occlusion", "ConvexVertexSize", 0.03f );
} // config

namespace tools
{
	namespace prv
	{
		static const char VertexPositionPropertyNameStr[] = "position";
		static const CName VertexPositionPropertyName(VertexPositionPropertyNameStr);
	}


	class VisConvexVertexEditableProxy final : public HelperObjectEditableProxy
	{
	public:
		static const CName PROXY_TYPE;

	public:
		VisConvexVertexEditableProxy( HelperObjectVisConvexMesh* meshObj, Uint16 faceIndex, Uint16 vertIndex, EditorObjectID editorObjectID )
			: HelperObjectEditableProxy( editorObjectID )
			, m_meshObj( meshObj )
			, m_faceIndex( faceIndex )
			, m_vertIndex( vertIndex )
		{ }

		virtual bool RestoreObject() const override
		{
			return EnsureExistance();
		}

		virtual const ViewClass GetViewClass() const override
		{
			if ( !EnsureExistance() )
			{
				return CName::NONE();
			}

			return m_vertexObj->GetClass()->GetName();
		}

		virtual const ViewClass FindCompatibleViewClass( const ViewClass viewClass ) const override
		{
			if ( !EnsureExistance() )
			{
				return CName::NONE();
			}

			if ( m_vertexObj->GetClass()->GetName() != viewClass )
			{
				return CName::NONE();
			}

			return m_vertexObj->GetClass()->GetName();
		}

		virtual red::SharedPtr<IEditableView> CreateView( const ViewClass viewClass ) const override
		{
			if ( !EnsureExistance() )
			{
				return red::SharedPtr<IEditableView>();
			}

			return m_vertexObj->CreateView( nullptr, IsReadOnly(), viewClass );
		}

		virtual void ToggleSelectionVisualization( const Bool isSelected ) const override
		{
			if ( !EnsureExistance() )
			{
				return;
			}

			m_vertexObj->m_isSelected = isSelected;
		}

		virtual Matrix GetWorldTransform() const override
		{
			if ( !EnsureExistance() )
			{
				return Matrix::IDENTITY;
			}

			const Matrix& mat = m_meshObj->GetTransform();
			const Vector3 transformedPoint = mat.TransformPoint( m_vertexObj->GetPosition() );

			Matrix matrix;
			matrix.SetIdentity();
			matrix.SetTranslation( transformedPoint );
			return matrix;
		}

		virtual Bool SetWorldTransform( const Matrix& localToWorld ) override
		{
			if ( !EnsureExistance() )
			{
				return false;
			}

			// don't move points if edges or face are selected. edge/face movement will handle it
			if (   m_meshObj->GetEdgeSubobject( m_faceIndex, m_vertexObj->GetNextEdgeIndex() )->m_isSelected
				|| m_meshObj->GetEdgeSubobject( m_faceIndex, m_vertexObj->GetPrevEdgeIndex() )->m_isSelected
				|| m_meshObj->GetFaceSubobject( m_faceIndex )->m_isSelected )
			{
				return true;
			}

			Matrix matInv = m_meshObj->GetInvertTransform();
			Vector3 transformedPoint = matInv.TransformPoint( Vector3( localToWorld.GetRow(3) ) );

			// Ensure the outline remains planar
			// #TODO: Might be nice if it were possible to change the face orientation by moving edges. For example, a
			// person could have a triangle, select a single vertex and move it anyways while still having a planar far.
			// Could work fine for a single vertex, but not if multiple vertices/edges/etc can be moved together.
			const auto& verts = m_meshObj->GetOccluderNode()->GetFaceVertices( m_faceIndex );
			if ( verts.Size() >= 3 )
			{
				// Try to avoid using the current vertex in determining the plane. Otherwise one could move a vertex in
				// a way that results in a near-degenerate triangle, skew the plane, and then change the plane of the
				// face.
				// #TODO: This may still cause issues when moving multiple vertices together... might be good to find a
				// more robust solution...
				const Vector3 v0 = verts[(m_vertIndex+1)%verts.Size()];
				const Vector3 v1 = verts[(m_vertIndex+2)%verts.Size()];
				const Vector3 v2 = verts[(m_vertIndex+3)%verts.Size()];
				Plane plane(v0, v1, v2);
				transformedPoint = plane.Project(transformedPoint);
			}

			m_vertexObj->SetPosition( transformedPoint );

			m_vertexObj->NotifyPropertyChanged( rtti::AccessPath( prv::VertexPositionPropertyNameStr ) );

			return true;
		}

		virtual Bool EnsureExistance() const override
		{
			m_vertexObj = m_meshObj->GetVertexSubobject( m_faceIndex, m_vertIndex );
			return m_vertexObj != nullptr;
		}

	private:

		// point we are editing
		mutable HelperObjectVisConvexMeshVertexPtr	m_vertexObj;

		HelperObjectVisConvexMesh*					m_meshObj;
		Uint16										m_faceIndex;
		Uint16										m_vertIndex;
	};

	const CName VisConvexVertexEditableProxy::PROXY_TYPE( "HelperObjectVisConvexMeshVertex" ); 


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	HelperObjectVisConvexMeshVertex::HelperObjectVisConvexMeshVertex( HelperObjectVisConvexMesh* meshObj, Uint16 faceIndex, Uint16 vertIndex )
		: m_meshObj( meshObj )
		, m_faceIndex( faceIndex )
		, m_vertIndex( vertIndex )
		, m_isSelected( false )
	{ }

	EditableProxyPtr HelperObjectVisConvexMeshVertex::Selectable_CreateProxy() const
	{
		return EditableProxyPtr( new VisConvexVertexEditableProxy( m_meshObj, m_faceIndex, m_vertIndex, Selectable_CreateEditorId() ) );
	}

	tools::EditorObjectID HelperObjectVisConvexMeshVertex::Selectable_CreateEditorId() const
	{
		const Uint16 index_plusOne = m_vertIndex + 1; // don't want 0 ids
		return EditorObjectID::Build( index_plusOne | (m_faceIndex<<16), VisConvexVertexEditableProxy::PROXY_TYPE );
	}


	Vector3 HelperObjectVisConvexMeshVertex::GetPosition() const
	{
		const auto& verts = m_meshObj->GetOccluderNode()->GetFaceVertices( m_faceIndex );
		return verts[m_vertIndex];
	}

	void HelperObjectVisConvexMeshVertex::SetPosition( const Vector3& position )
	{
		// #TODO: vertex movement notification should be send after LMB is released, so occluder is not recalculated
		// every mouse move. or at the very least, once per move instead of for each selected vertex

		auto node = m_meshObj->GetOccluderNode();
		node->MoveVertex( m_faceIndex, m_vertIndex, position, false );
	}


	Uint16 HelperObjectVisConvexMeshVertex::GetNextEdgeIndex() const
	{
		return m_vertIndex;
	}

	Uint16 HelperObjectVisConvexMeshVertex::GetPrevEdgeIndex() const
	{
		const Uint32 numVerts = m_meshObj->GetOccluderNode()->GetFaceSize( m_faceIndex );
		return ( m_vertIndex + numVerts - 1 ) % numVerts;
	}


	void HelperObjectVisConvexMeshVertex::OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder ) const
	{
		const Bool isVertSelected = m_isSelected;
		const Bool isEdgeSelected = m_meshObj->GetEdgeSubobject(m_faceIndex, GetNextEdgeIndex())->m_isSelected || m_meshObj->GetEdgeSubobject(m_faceIndex, GetPrevEdgeIndex())->m_isSelected;
		const Bool isFaceSelected = m_meshObj->GetFaceSubobject(m_faceIndex)->m_isSelected;

		builder.BindSelectable( this );
		builder.SetColor( HelperObjectVisConvexMesh::GetSubobjectColor( isVertSelected, isEdgeSelected||isFaceSelected ) );
		builder.EnableOverlay( true );

		const Vector3 pos = GetPosition();
		const Vector3 transformedPos = m_meshObj->GetTransform().TransformPoint( pos );
		Float screenSpaceScale = viewport.CalcScreenSpaceScale( transformedPos );
		if ( isVertSelected || isEdgeSelected || isFaceSelected )
		{
			// If the vertex, or some related object, is selected, bump up its size. Maybe makes it a bit easier to work
			// with and select.
			screenSpaceScale *= 1.75f;
		}
		Float controlPointSize = math::Max( 0.0f, screenSpaceScale * Config::cvVisConvexShapeVertexSize.Get() );
		const Vector3 extrude = Vector3( controlPointSize, controlPointSize, controlPointSize );

		builder.AddSolidBox( transformedPos - extrude, transformedPos + extrude );

		builder.EnableOverlay( false );
	}

	void HelperObjectVisConvexMeshVertex::GetCustomEditablePropeties( red::DynArray< rtti::ClassEditablePropertyInfo >& outRootProperties ) const
	{
		rtti::ClassEditablePropertyInfo info;
		info.m_name = prv::VertexPositionPropertyName;
		info.m_type = GetTypeObject< Vector3 >();
		outRootProperties.PushBack( info );
	}

	const Bool HelperObjectVisConvexMeshVertex::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const
	{
		if ( propertyName == prv::VertexPositionPropertyName )
		{
			const Vector3 position = GetPosition();
			return GetTypeObject< Vector3 >()->ReadValue( ctx, &position, restOfThePath, outValue );
		}

		return false;
	}

	const Bool HelperObjectVisConvexMeshVertex::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue )
	{
		if ( propertyName == prv::VertexPositionPropertyName )
		{
			Vector3 oldPosition = GetPosition();
			Vector3 position = oldPosition;
			if ( !GetTypeObject< Vector3 >()->WriteValue( ctx, &position, restOfThePath, *newValue ) )
			{
				return false;
			}

			SetPosition( position );
			return true;
		}

		return false;
	}

}

#endif