/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "helperObjectVisConvexMeshFace.h"
#include "helperObjectVisConvexMesh.h"
#include "../../common/core/include/configVar.h"

// TEMPORARY
#if 0

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( HelperObjectVisConvexMeshFace, tools );
	RTTI_PARENT_TYPE( HelperObjectSubobject );
	RTTI_PROPERTY( m_faceIndex ).readOnly();
RTTI_END_TYPE();


namespace Config
{
	TConfigVar< Float, Validation::FloatRange< 1, 10, 100> > cvVisConvexMeshFaceSize( "Editor/Occlusion", "ConvexFaceSize", 0.04f );
} // config

namespace tools
{

	namespace prv
	{
		static const char FaceOrientationPropertyNameStr[] = "orientation";
		static const char FaceCenterPropertyNameStr[] = "center";
		static const CName FaceOrientationPropertyName(FaceOrientationPropertyNameStr);
		static const CName FaceCenterPropertyName(FaceCenterPropertyNameStr);
	}


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	class VisConvexFaceEditableProxy final : public HelperObjectEditableProxy
	{
	public:
		static const CName PROXY_TYPE;

	public:
		VisConvexFaceEditableProxy( HelperObjectVisConvexMesh* convexObj, Uint16 faceIndex, EditorObjectID editorObjectID )
			: HelperObjectEditableProxy( editorObjectID )
			, m_meshObj( convexObj )
			, m_faceIndex( faceIndex )
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
				if ( isSelected != m_faceObj->m_isSelected )
				{
					if ( isSelected )
					{
						m_faceObj->Begin();
					}
					m_faceObj->m_isSelected = isSelected;
				}
			}
		}

		virtual const ViewClass GetViewClass() const override
		{
			if ( !EnsureExistance() )
			{
				return CName::NONE();
			}

			return m_faceObj->GetClass()->GetName();
		}

		virtual const ViewClass FindCompatibleViewClass( const ViewClass viewClass ) const override
		{
			if ( !EnsureExistance() )
			{
				return CName::NONE();
			}

			if ( m_faceObj->GetClass()->GetName() != viewClass )
			{
				return CName::NONE();
			}

			return m_faceObj->GetClass()->GetName();
		}

		virtual red::SharedPtr<IEditableView> CreateView( const ViewClass viewClass ) const override
		{
			if ( !EnsureExistance() )
			{
				return red::SharedPtr<IEditableView>();
			}

			return m_faceObj->CreateView( nullptr, IsReadOnly(), viewClass );
		}

		virtual Matrix GetWorldTransform() const override
		{
			if ( !EnsureExistance() )
			{
				return Matrix::IDENTITY;
			}

			// Edited transform is in node-local space, so need to switch to world.
			const Matrix& nodeTransform = m_meshObj->GetTransform();
			return m_faceObj->GetEditedTransform() * nodeTransform;
		}

		virtual Bool SetWorldTransform( const Matrix& localToWorld ) override
		{
			if ( !EnsureExistance() )
			{
				return false;
			}

			// Edited transform is in node-local space, so switch from world to that.
			const Matrix& nodeInvTransform = m_meshObj->GetInvertTransform();
			m_faceObj->SetEditedTransform( localToWorld * nodeInvTransform );

			// #TODO: Could detect what property has been changed...
			m_faceObj->NotifyPropertyChanged( rtti::AccessPath(prv::FaceCenterPropertyNameStr) );
			m_faceObj->NotifyPropertyChanged( rtti::AccessPath(prv::FaceOrientationPropertyNameStr) );

			return true;
		}

		virtual Bool EnsureExistance() const override
		{
			m_faceObj = m_meshObj->GetFaceSubobject( m_faceIndex );
			return m_faceObj != nullptr;
		}


	private:
		mutable HelperObjectVisConvexMeshFacePtr	m_faceObj;

		HelperObjectVisConvexMesh*					m_meshObj;
		Uint16										m_faceIndex;
	};

	const CName VisConvexFaceEditableProxy::PROXY_TYPE( "HelperObjectVisConvexMeshFace" ); 


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	HelperObjectVisConvexMeshFace::HelperObjectVisConvexMeshFace( HelperObjectVisConvexMesh* meshObj, Uint16 faceIndex )
		: m_meshObj( meshObj )
		, m_faceIndex( faceIndex )
		, m_isSelected( false )
	{
		Begin();
	}

	void HelperObjectVisConvexMeshFace::OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder ) const
	{
		builder.BindSelectable( this );

		const auto& transform = m_meshObj->GetTransform();
		builder.PushLocalTransform();
		builder.SetLocalTransform( transform );

		// Face fill follows different coloring than normal. Useful to have it normally semi transparent. When selected,
		// make in non-transparent, so that it's easier to see relation to other faces.
		Color color = Color::LIGHT_BLUE;
		if ( !m_meshObj->GetOccluderNode()->IsOccluderValid() )
		{
			color = Color::RED;
		}
		if ( m_isSelected )
		{
			color = Color::Lerp( 0.5f, color, Color::YELLOW );
		}
		else
		{
			builder.EnableTransparency( true );
			color.A = 127;
		}
		builder.SetColor( color );

		const auto& verts = m_meshObj->GetOccluderNode()->GetFaceVertices( m_faceIndex );
		builder.AddSolidPolygon( verts.TypedData(), verts.Size() );
		if ( m_meshObj->GetOccluderNode()->GetTwoSided() )
		{
			builder.AddSolidPolygon( verts.TypedData(), verts.Size(), true );
		}

		builder.PopLocalTransform();
		builder.EnableTransparency( false );

		builder.EnableOverlay( true );
		// Draw a selection helper in the face's center. This makes it easier to select a face when it's behind some
		// scene geometry.
		{
			// #TODO: This isn't perfect... it uses the last cached editedTransform, which won't change if a vertex or
			// edge is edited separately. In that case the selection helper will stay still, until the face is selected
			// and then snap to a new location.

			// #TODO: Color appropriately if vertex or edge is also selected.
			const Bool isFaceSelected = m_isSelected;
			Bool isVertOrEdgeSelected = false;
			for ( Uint32 i = 0; i < verts.Size(); ++i )
			{
				if ( m_meshObj->GetVertexSubobject( m_faceIndex, i )->m_isSelected || m_meshObj->GetEdgeSubobject( m_faceIndex, i )->m_isSelected )
				{
					isVertOrEdgeSelected = true;
					break;
				}
			}
			builder.SetColor( HelperObjectVisConvexMesh::GetSubobjectColor( isFaceSelected, isVertOrEdgeSelected ) );

			const Vector3 pos = m_editedTransform.GetTranslation();
			const Vector3 transformedPos = m_meshObj->GetTransform().TransformPoint( pos );
			Float screenSpaceScale = viewport.CalcScreenSpaceScale( transformedPos );
			Float controlPointSize = math::Max( 0.0f, screenSpaceScale * Config::cvVisConvexMeshFaceSize.Get() );
			const Vector3 extrude = Vector3( controlPointSize, controlPointSize, controlPointSize );

			builder.AddSolidBox( transformedPos - extrude, transformedPos + extrude );

			builder.SetColor( Color::WHITE );
			builder.AddWorldText( transformedPos, -3, 6, "F" );
		}
		builder.EnableOverlay( false );
	}

	EditableProxyPtr HelperObjectVisConvexMeshFace::Selectable_CreateProxy() const
	{
		return EditableProxyPtr( new VisConvexFaceEditableProxy( m_meshObj, m_faceIndex, Selectable_CreateEditorId() ) );
	}

	tools::EditorObjectID HelperObjectVisConvexMeshFace::Selectable_CreateEditorId() const
	{
		const Uint16 index_plusOne = m_faceIndex + 1; // don't want 0 ids
		return EditorObjectID::Build( index_plusOne, VisConvexFaceEditableProxy::PROXY_TYPE );
	}



	void HelperObjectVisConvexMeshFace::GetCustomEditablePropeties( red::DynArray< rtti::ClassEditablePropertyInfo >& outRootProperties ) const
	{
		rtti::ClassEditablePropertyInfo info;
		info.m_name = prv::FaceOrientationPropertyName;
		info.m_type = GetTypeObject< EulerAngles >();
		outRootProperties.PushBack( info );

		info.m_name = prv::FaceCenterPropertyName;
		info.m_type = GetTypeObject< Vector3 >();
		outRootProperties.PushBack( info );
	}

	const Bool HelperObjectVisConvexMeshFace::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const
	{
		if ( propertyName == prv::FaceOrientationPropertyName )
		{
			Vector translation, scale;
			Matrix rotation;
			Matrix::Decompose(m_editedTransform, translation, rotation, scale);
			EulerAngles orient = rotation.ToEulerAngles();
			return GetTypeObject< EulerAngles >()->ReadValue( ctx, &orient, restOfThePath, outValue );
		}
		else if ( propertyName == prv::FaceCenterPropertyName )
		{
			Vector3 center = m_editedTransform.GetRow(3);
			return GetTypeObject< Vector3 >()->ReadValue( ctx, &center, restOfThePath, outValue );
		}

		return false;
	}

	const Bool HelperObjectVisConvexMeshFace::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue )
	{
		if ( propertyName == prv::FaceOrientationPropertyName )
		{
			Vector translation, scale;
			Matrix rotation;
			Matrix::Decompose(m_editedTransform, translation, rotation, scale);
			// #TODO: make sure scale is 1.0?
			EulerAngles oldOrient = rotation.ToEulerAngles();
			EulerAngles newOrient = oldOrient;
			if ( !GetTypeObject< EulerAngles >()->WriteValue( ctx, &newOrient, restOfThePath, *newValue ) )
			{
				return false;
			}

			Matrix newTransform = newOrient.ToMatrix();
			newTransform.SetTranslation( translation );

			SetEditedTransform( newTransform );

			return true;
		}
		else if ( propertyName == prv::FaceCenterPropertyName )
		{
			Vector3 oldCenter = m_editedTransform.GetRow(3);
			Vector3 newCenter = oldCenter;
			if ( !GetTypeObject< Vector3 >()->WriteValue( ctx, &newCenter, restOfThePath, *newValue ) )
			{
				return false;
			}

			Matrix m = m_editedTransform;
			m.SetTranslation( m.GetTranslationRef() + (newCenter - oldCenter) );
			SetEditedTransform( m );

			return true;
		}

		return false;
	}


	void HelperObjectVisConvexMeshFace::SetEditedTransform( const Matrix& transform )
	{
		const Uint32 numVerts = m_originalVertices.Size();
		red::DynArray<Vector3> newPositions( numVerts );
		for ( Uint32 i = 0; i < numVerts; ++i )
		{
			newPositions[i] = transform.TransformPoint( m_originalVertices[i] );
		}

		auto node = m_meshObj->GetOccluderNode();
		node->MoveVertices( m_faceIndex, newPositions, false );

		m_editedTransform = transform;
	}


	void HelperObjectVisConvexMeshFace::RefreshCachedData()
	{
		// We need to come up with some reasonable transform for it. The faces in the occluder itself don't have their
		// own transforms, but we just need something to make editing a bit more straightforward (like having the direct
		// editing in the property grid). So we take the face center for the translation, orient Y axis along the face
		// normal, and X runs along the first edge.

		const auto& vertices = m_meshObj->GetOccluderNode()->GetFaceVertices(m_faceIndex);

		if ( vertices.Size() >= 3 )
		{
			const Vector v0 = vertices[0];
			const Vector v1 = vertices[1];
			const Vector v2 = vertices[2];

			const Vector normal = Vector::Cross( v1-v0, v2-v0, 0.0f ).Normalized4();
			const Vector axisU = ( v1-v0 ).Normalized4();
			const Vector axisV = Vector::Cross( axisU, normal, 0.0f );

			Vector position = Vector::ZEROS;
			for ( const Vector& v : vertices )
			{
				position += v * (1.0f / vertices.Size());
			}

			m_editedTransform.SetIdentity();
			m_editedTransform.SetRow( 0, axisU );
			m_editedTransform.SetRow( 1, normal );
			m_editedTransform.SetRow( 2, axisV );
			m_editedTransform.SetRow( 3, position );
		}
		else
		{
			m_editedTransform.SetIdentity();
		}
	}


	void HelperObjectVisConvexMeshFace::Begin()
	{
		RefreshCachedData();

		// Take the original vertices, and transform them into the face's space. This way we can use the updated
		// transforms to put things back to object space.
		const auto& vertices = m_meshObj->GetOccluderNode()->GetFaceVertices(m_faceIndex);
		const Uint32 numVertices = vertices.Size();
		m_originalVertices.Resize( numVertices );
		const Matrix inverted = m_editedTransform.Inverted();
		for ( Uint32 i = 0; i < numVertices; ++i )
		{
			m_originalVertices[i] = inverted.TransformPoint( vertices[i] );
		}
	}

}

#endif