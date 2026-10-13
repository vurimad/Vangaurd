/* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.*/

#include "build.h"
#include "physicsShape.h"
#include "physicsActor.h"
#include "physicsEditorScene.h"
#include "physicsBackendHelper.h"

#include "../../backendEngine/include/editableProxy.h"

#include "../../../common/redReflection/include/convexHullBuilder.h"
#include "../../../common/redMemory/include/sharedPtrUtils.hpp"
#include "../../../common/redFileSystem/include/file.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/redReflection/include/mathCutCone.h"
#include "../../../common/physics/include/physicsCollider.h"
#include "../../../common/physics/include/physXNatives.h"
#include "../../../common/renderData/include/drawer.h"
#include "../../../common/physics/include/physicsMaterialLibrary.h"
#include "../../../common/physics/include/physicsCollisionFilterResource.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"

// Geometry type
RTTI_BEGIN_ENUM_IN_NAMESPACE( EPhysicsShapeGeometryType, physics )
RTTI_ENUM_OPTION_ALIAS( "Box", EPST_Box );
RTTI_ENUM_OPTION_ALIAS( "Sphere", EPST_Sphere );
RTTI_ENUM_OPTION_ALIAS( "Capsule", EPST_Capsule );
RTTI_ENUM_OPTION_ALIAS( "Convex", EPST_Convex );
RTTI_ENUM_OPTION_ALIAS( "TriMesh", EPST_TriMesh );
RTTI_ENUM_OPTION_ALIAS( "Invalid", EPST_Invalid );
RTTI_ENUM_OPTION_ALIAS( "Max", EPST_Max );
RTTI_END_ENUM();


// SShapeGeometryBase

RTTI_BEGIN_TYPE_IN_NAMESPACE( ShapeGeometryBase, physics );
RTTI_PARENT_TYPE( ISerializable )
RTTI_PROPERTY( m_shapeType );
RTTI_END_TYPE();


// !SShapeGeometryBase

// ShapeGeometryBox

RTTI_BEGIN_TYPE_IN_NAMESPACE( ShapeGeometryBox, physics );
RTTI_PARENT_TYPE( physics::ShapeGeometryBase )
RTTI_PROPERTY_CATEGORY( "1. Box" );
RTTI_PROPERTY( m_halfExtents ).editable().range( k_physicsAbsoluteMinSize, RED_FLT_MAX, true ).tooltip( "Width / Height / Depth of the box" );
RTTI_END_TYPE();


// !ShapeGeometryBox

// ShapeGeometrySphere

RTTI_BEGIN_TYPE_IN_NAMESPACE( ShapeGeometrySphere, physics );
RTTI_PARENT_TYPE( physics::ShapeGeometryBase )
RTTI_PROPERTY_CATEGORY( "1. Sphere" );
RTTI_PROPERTY( m_radius ).editable().range( k_physicsAbsoluteMinSize, RED_FLT_MAX, true ).tooltip( "Radius of the sphere" );
RTTI_END_TYPE();


// !ShapeGeometrySphere

// ShapeGeometryCapsule

RTTI_BEGIN_TYPE_IN_NAMESPACE( ShapeGeometryCapsule, physics );
RTTI_PARENT_TYPE( physics::ShapeGeometryBase )
RTTI_PROPERTY_CATEGORY( "1. Capsule" );
RTTI_PROPERTY( m_radius ).editable().range( k_physicsAbsoluteMinSize, RED_FLT_MAX, true ).tooltip( "Radius of the capsule half spheres" );
RTTI_PROPERTY( m_height ).editable().range( k_physicsAbsoluteMinSize, RED_FLT_MAX, true ).tooltip( "Height of the capsule (distance between center of the half spheres." );
RTTI_END_TYPE();


// !ShapeGeometryCapsule

// ShapeGeometryConvex

RTTI_BEGIN_TYPE_IN_NAMESPACE( ShapeGeometryConvex, physics );
RTTI_PARENT_TYPE( physics::ShapeGeometryBase )
RTTI_PROPERTY_CATEGORY( "1. Convex" );
RTTI_PROPERTY( m_vertexPositions );
RTTI_PROPERTY( m_vertexCount ).readOnly();
RTTI_PROPERTY( m_indices );
RTTI_END_TYPE();

// !ShapeGeometryConvex

// ShapeGeometryTriMesh

RTTI_BEGIN_TYPE_IN_NAMESPACE( ShapeGeometryTriMesh, physics );
RTTI_PARENT_TYPE( physics::ShapeGeometryBase )
RTTI_PROPERTY( m_vertices ).readOnly();
RTTI_PROPERTY( m_indices ).readOnly();
RTTI_PROPERTY( m_physicalMaterials ).readOnly();
RTTI_PROPERTY( m_physicalMaterialIndices ).readOnly();
RTTI_END_TYPE();


// !ShapeGeometryTriMesh

// PhysicsShape

RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsShape, physics );
RTTI_PARENT_TYPE( physics::PhysicsBase );
RTTI_PROPERTY_CATEGORY( "2. Shape properties" ); 
RTTI_PROPERTY( m_parentId ).editable().tooltip( "Id of an actor this shape belongs to." );
RTTI_PROPERTY( m_geometry ).inlined().clearable( false ).resetable( false );
RTTI_PROPERTY( m_localTransform ).editable().tooltip( "Transform of the shape, relative to the parent actor's pose." );
RTTI_PROPERTY( m_parentL2W );
RTTI_PROPERTY( m_parentL2WInverted );
RTTI_PROPERTY( m_material ).editable().customEditor( "PhysicsMaterial" ).tooltip( "Physical material to use for this shape." );
RTTI_PROPERTY( m_materialApperanceOverrides ).inlined().resizable().tooltip( "Physical materials to use for this shape when specified mesh appearance is chosen." );
RTTI_PROPERTY( m_isQueryShapeOnly ).inlined().tooltip( 
	"The body's not simulated, meaning that nothing will collide with it, player will freely pass through it etc.\n" \
	"However, any physical queries will report that as if it was a normal physical actor" );
RTTI_PROPERTY( m_isObstacle );
RTTI_PROPERTY( m_volumeModifier ).editable().range( 0.001f, 10.0f ).tooltip( "(0.001, 10.0) value determining how full the collider is - for example, if we have a box shape " \
	"representing hollow cardbox, we need to pass a low value, so only fraction of the box volume is used in mass calculation. It can also be used to \"inflate\" the volume and increase the mass by going above 1.0f." );
RTTI_PROPERTY( m_filterData ).inlined().tooltip( "Physics filter to be used for the shape." );
RTTI_PROPERTY( m_extractedScale ).notCooked();
RTTI_END_TYPE();

// !PhysicsShape
namespace physics
{
	// Shape geometry

	void ShapeGeometryBase::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		m_wasModified = true;
	}

	void ShapeGeometryTriMesh::GetRenderingData( red::DynArray< DebugVertex >& vertices, red::DynArray< Uint16 >& indices )
	{
		if( m_physicalMaterials.Size() > 0 )
		{
			vertices.Reserve( m_vertices.Size() );
			indices.Reserve( m_vertices.Size() );
			const Uint32 numIndices = m_indices.Size();
			Uint32 faceIndex = 0;
			auto& material_library = GetMaterialLibrary();
			for( Uint32 i = 0; i < numIndices; i += 3, ++faceIndex )
			{
				auto material = material_library.FindMaterial( m_physicalMaterials[ m_physicalMaterialIndices[ faceIndex ] ] );
				const auto& matRes = material ? *material : material_library.GetDefaultMaterial();
				Color color = matRes.m_color;
				indices.PushBack( vertices.Size() );
				vertices.PushBack( DebugVertex( m_vertices[ m_indices[ i ] ], color ) );
				indices.PushBack( vertices.Size() );
				vertices.PushBack( DebugVertex( m_vertices[ m_indices[ i + 1 ] ], color ) );
				indices.PushBack( vertices.Size() );
				vertices.PushBack( DebugVertex( m_vertices[ m_indices[ i + 2 ] ], color ) );
			}
		}
		else
		{
			vertices.Reserve( m_vertices.Size() );
			for( Uint32 index : m_vertices.Indices() )
			{
				vertices.PushBack( DebugVertex( m_vertices[ index ], Color::WHITE() ) );
			}
			indices = m_indices;
		}

		return;
	}

	//

	PhysicsShape::PhysicsShape()
		: PhysicsBase( 0, EPBT_Shape, nullptr )
		, m_localTransform( Matrix::IDENTITY() )
		, m_parentL2W( Matrix::IDENTITY() )
		, m_parentL2WInverted( Matrix::IDENTITY() )
		, m_parentId( 0 )
		, m_extractedScale( Vector4::ONES() )
		, m_isQueryShapeOnly( false )
	{}

	PhysicsShape::PhysicsShape( PhysicsShapeParams params )
		: PhysicsBase( params.m_id, EPBT_Shape, params.m_editorParent )
		, m_localTransform( params.m_localTransform )
		, m_parentL2W( params.m_parentL2W )
		, m_parentL2WInverted( params.m_parentL2W.OrthonormInverted() )
		, m_parentId( params.m_parentId )
		, m_extractedScale( Vector4::ONES() )
		, m_isQueryShapeOnly( false )
	{
		UpdateLocalTransform( params.m_localTransform );
	}

	PhysicsShape::PhysicsShape( const THandle<PhysicsShape>& copyFrom )
		: PhysicsBase( copyFrom->GetPhysicsObjectID(), EPBT_Shape, copyFrom->GetEditorScene() )
		, m_localTransform( copyFrom->m_localTransform )
		, m_parentL2W( copyFrom->m_parentL2W )
		, m_parentL2WInverted( copyFrom->m_parentL2W.OrthonormInverted() )
		, m_parentId( copyFrom->m_parentId )
		, m_extractedScale( Vector4::ONES() )
		, m_geometry( copyFrom->m_geometry )
		, m_isQueryShapeOnly( false )
	{
		UpdateLocalTransform( copyFrom->m_localTransform );

	}

	PhysicsShape::~PhysicsShape()
	{}

	void PhysicsShape::SetLocalToWorld( const Matrix& localToWorld )
	{
		UpdateLocalTransform( localToWorld * m_parentL2WInverted );
	}

	Bool PhysicsShape::SetScale( const Vector3& newScale )
	{
		if( !m_geometry ) 
			return false;

		Vector4 scaleDiff = Vector4::ZERO_3D_POINT();
		auto checkAxisChange = [ ] ( Float axisValue, Float previousAxisValue, Float& result )
		{
			// Scaling starts with 1.0f, move it to 0, because we are performing addition
			if( math::Abs( axisValue - 1.0f ) > FLT_EPSILON )
			{
				result = axisValue - previousAxisValue;
				return true;
			}
			return false;
		};

		Bool change = checkAxisChange( newScale.X, m_extractedScale.X, scaleDiff.X );
		change = checkAxisChange( newScale.Y, m_extractedScale.Y, scaleDiff.Y ) || change;
		change = checkAxisChange( newScale.Z, m_extractedScale.Z, scaleDiff.Z ) || change;

		Bool shapeChanged = true;
		if( change )
		{
			m_extractedScale = newScale;
			Vector4 position = Vector4::ONES();
			if( m_geometry->m_shapeType == EPST_Box )
			{
				ShapeGeometryBox* boxGeom = (ShapeGeometryBox*)m_geometry.Get();

				boxGeom->m_halfExtents.X = math::Max( k_physicsAbsoluteMinSize, math::Abs(boxGeom->m_halfExtents.X + scaleDiff.X) );
				boxGeom->m_halfExtents.Y = math::Max( k_physicsAbsoluteMinSize, math::Abs(boxGeom->m_halfExtents.Y + scaleDiff.Y) );
				boxGeom->m_halfExtents.Z = math::Max( k_physicsAbsoluteMinSize, math::Abs(boxGeom->m_halfExtents.Z + scaleDiff.Z) );
				boxGeom->NotifyPropertyChanged(rtti::AccessPath()[ "halfExtents" ]);
			}
			else if( m_geometry->m_shapeType == EPST_Sphere )
			{
				ShapeGeometrySphere* sphereGeom = (ShapeGeometrySphere*)m_geometry.Get();

				sphereGeom->m_radius = math::Max( k_physicsAbsoluteMinSize, math::Abs( sphereGeom->m_radius + scaleDiff.X + scaleDiff.Y + scaleDiff.Z ));
				sphereGeom->NotifyPropertyChanged(rtti::AccessPath()[ "radius" ]);
			}
			else if( m_geometry->m_shapeType == EPST_Capsule )
			{
				ShapeGeometryCapsule* capsuleGeom = (ShapeGeometryCapsule*)m_geometry.Get();
				capsuleGeom->m_radius = math::Max( k_physicsAbsoluteMinSize, math::Abs( capsuleGeom->m_radius + scaleDiff.X + scaleDiff.Y ) );
				capsuleGeom->m_height = math::Max( k_physicsAbsoluteMinSize, math::Abs( capsuleGeom->m_height + scaleDiff.Z ) );
				capsuleGeom->NotifyPropertyChanged(rtti::AccessPath()[ "radius" ]);
				capsuleGeom->NotifyPropertyChanged(rtti::AccessPath()[ "height" ]);
			}
			else if( m_geometry->m_shapeType == EPST_Convex )
			{
				ShapeGeometryConvex* convexGeom = (ShapeGeometryConvex*)m_geometry.Get();
				Matrix scale = Matrix::IDENTITY();
				scale.SetScale( Vector4( 1.0f + scaleDiff.X, 1.0f + scaleDiff.Y, 1.0f + scaleDiff.Z, 1.0f ) );
				Uint32 vertexCount = convexGeom->m_vertexPositions.Size();
				for( Uint32 i = 0; i < vertexCount; ++i )
				{
					convexGeom->m_vertexPositions[ i ] = scale.TransformPoint( convexGeom->m_vertexPositions[ i ] );
				}
				shapeChanged = physics::PhysicsBackendHelper::GetColliderFromEditorGeometry( HandleFromThis<physics::PhysicsShape>() ) != nullptr;
			}			
			else if( m_geometry->m_shapeType == EPST_TriMesh )
			{
				ShapeGeometryTriMesh* trimeshGeom = (ShapeGeometryTriMesh*)m_geometry.Get();
				RED_LOG( "TriMesh geometry scaling not supported yet." );
			}
			SetFlag( EPBF_WasModified, true );
		}

		return shapeChanged;
	}

	Vector3 PhysicsShape::GetScale() const
	{
		return m_extractedScale;
	}

	void PhysicsShape::OnParentLocalToWorldChanged(Matrix parentL2W)
	{
		m_parentL2W = parentL2W;
		m_parentL2WInverted = parentL2W.FullInverted();
		PhysicsBase::SetLocalToWorld( m_localTransform * m_parentL2W );
	}

	void PhysicsShape::UpdateLocalTransform( Matrix localTransform )
	{
		m_localTransform = localTransform;
		PhysicsBase::SetLocalToWorld( m_localTransform * m_parentL2W );
		NotifyPropertyChanged(rtti::AccessPath()[ "localTransform" ]);
	}

	void PhysicsShape::OnUpdate()
	{
		if( m_geometry && m_geometry->m_wasModified )
		{
			SetFlag( EPBF_WasModified, true );
			SetFlag( EPSF_RequestGeometryRebuild, true );
			UpdateBounds();
			GetEditorScene()->RecomputeCOM( { GetActorParent()->GetEditorObjectID() } );
			m_geometry->m_wasModified = false;
		}
		PhysicsBase::OnUpdate();
	}

	void PhysicsShape::OnRender( rend::IDebugDrawer& dd, PhysicsRendererOptionsPtr options )
	{
		if( options->m_shapes )
		{
			Color color = GetColorFromParent();
			if( options->m_materials )
			{
				if( !IsFlagSet( EPSF_IsMaterialRenderingEnabled ) )
				{
					SetFlag( EPSF_IsMaterialRenderingEnabled, true );
					if( m_geometry->m_shapeType == EPST_TriMesh )
					{
						SetFlag( EPBF_RequestRenderingRebuild, true );
						return;
					}
				}
				if( m_geometry->m_shapeType == EPST_TriMesh )
				{
					ShapeGeometryTriMesh* triMeshGeom = (ShapeGeometryTriMesh*)m_geometry.Get();
					if( triMeshGeom->m_physicalMaterials.Size() > 0 )
					{
						color = Color::WHITE();
					}
					else
					{
						auto& matLib = GetMaterialLibrary();
						const auto& matRes = matLib.FindMaterial( m_material );
						color = matRes->m_color;
					}
				}
				else
				{
					auto& matLib = GetMaterialLibrary();
					const auto& matRes = matLib.FindMaterial( m_material );
					color = matRes->m_color;
				}
			}
			else
			{
				if( IsFlagSet( EPSF_IsMaterialRenderingEnabled ) )
				{
					SetFlag( EPSF_IsMaterialRenderingEnabled, false );
					if( m_geometry->m_shapeType == EPST_TriMesh )
					{
						SetFlag( EPBF_RequestRenderingRebuild, true );
					}
				}
			}

			if( m_isObstacle )
			{
				color = Color::CYAN();
			}

			color.A = m_alpha;
			m_physicsRenderHelper->SetColor( color );
			PhysicsBase::OnRender( dd, options );
		}
	}

	void PhysicsShape::OnPostLoad( const PostLoadContext& context )
	{
		PhysicsBase::OnPostLoad( context );
		if( THandle<physics::ShapeGeometryTriMesh> trimesh = Cast< physics::ShapeGeometryTriMesh >( m_geometry ) )
		{
			trimesh->m_numTriangles = trimesh->m_indices.Size() / 3;
		}
		else if( THandle<physics::ShapeGeometryConvex> convex = Cast< physics::ShapeGeometryConvex >( m_geometry ) )
		{
			convex->m_vertexCount = convex->m_vertexPositions.Size();
		}

		if( math::EqualsZero( m_volumeModifier ) )
		{
			m_volumeModifier = 1.0f; // reset to default
		}
		RestoreLocalToWorld();
	}

	void PhysicsShape::OnRestore( PhysicsEditorScene* scene )
	{
		PhysicsBase::OnRestore( scene );
		UpdateBounds();
	}

	void PhysicsShape::RestoreLocalToWorld()
	{
		PhysicsActorPtr parent = GetActorParent();
		if( parent )
		{
			m_parentL2W = parent->GetLocalToWorld();
			m_parentL2WInverted = m_parentL2W.OrthonormInverted();
			UpdateLocalTransform( m_localTransform );
		}
	}

	void PhysicsShape::UpdateBounds()
	{
		if( !m_geometry )
			return;

		Vector4 position = Vector4::ZEROS();
		m_bounds = Box::EMPTY();
		if( m_geometry->m_shapeType == EPST_Box )
		{
			ShapeGeometryBox* boxGeom = (ShapeGeometryBox*)m_geometry.Get();

			Vector4 right = Vector3::EX();
			Vector4 forward = Vector3::EZ();
			Vector4 up = Vector3::EY();

			right *= boxGeom->m_halfExtents.X;
			forward *= boxGeom->m_halfExtents.Z;
			up *= boxGeom->m_halfExtents.Y;

			Vector4 diag = ( right + forward + up );
			Vector4 min = position - diag;
			Vector4 max = position + diag;
			m_bounds = Box( min, max );
		}
		else if( m_geometry->m_shapeType == EPST_Sphere )
		{
			ShapeGeometrySphere* sphereGeom = (ShapeGeometrySphere*)m_geometry.Get();

			m_bounds = Box( position, sphereGeom->m_radius );
		}
		else if( m_geometry->m_shapeType == EPST_Capsule )
		{
			ShapeGeometryCapsule* capsuleGeom = (ShapeGeometryCapsule*)m_geometry.Get();

			position.Z -= ( capsuleGeom->m_height / 2.0f ); // Engine capsule position is at it's base, physics in the middle
			FixedCapsule capsule = FixedCapsule( position, capsuleGeom->m_radius, capsuleGeom->m_height );

			Vector4 pointA = capsule.CalcPointA();
			Vector4 pointB = capsule.CalcPointB();

			pointA.X -= capsuleGeom->m_radius;
			pointA.Y -= capsuleGeom->m_radius;

			pointB.X += capsuleGeom->m_radius;
			pointB.Y += capsuleGeom->m_radius;

			m_bounds = Box( pointA, pointB );
		}
		else if( m_geometry->m_shapeType == EPST_Convex )
		{
			ShapeGeometryConvex* convexGeom = (ShapeGeometryConvex*)m_geometry.Get();
			for( auto& v : convexGeom->m_vertexPositions )
			{
				m_bounds.AddPoint(v);
			}
		}
		else if( m_geometry->m_shapeType == EPST_TriMesh )
		{
			ShapeGeometryTriMesh* triMesh = (ShapeGeometryTriMesh*)m_geometry.Get();
			for( const Vector3& v : triMesh->m_vertices )
			{
				m_bounds.AddPoint( v );
			}
		}
	}

	void PhysicsShape::OnParentChanged()
	{
		PhysicsActorPtr newParent = GetActorParent();
		if( newParent )
		{
			OnParentLocalToWorldChanged( newParent->GetLocalToWorld() );
		}
	}

	void PhysicsShape::PrepareForRendering( PhysicsRendererOptionsPtr options )
	{
		PhysicsBase::PrepareForRendering( options );
		m_alpha = (Uint8)( options->m_shapeAlpha * 255 );
		if( !IsFlagSet( EPBF_RequestRenderingRebuild ) )
		{
			return;
		}

		if( !m_physicsRenderHelper )
		{
			m_physicsRenderHelper = red::CreateUniquePtr< PhysicsShapeRenderer >();
		}
		m_physicsRenderHelper->SetLocalToWorld( GetLocalToWorld() );

		Vector4 position = Vector4::ZEROS();

		if( !m_geometry )
		{
			return;
		}

		m_physicsRenderHelper->Reset();

		if( m_geometry->m_shapeType == EPST_Box )
		{
			ShapeGeometryBox* boxGeom = (ShapeGeometryBox*)m_geometry.Get();

			Vector4 right = Vector3::EX();
			Vector4 forward = Vector3::EZ();
			Vector4 up = Vector3::EY();

			right *= boxGeom->m_halfExtents.X * 2.0f;
			forward *= boxGeom->m_halfExtents.Z * 2.0f;
			up *= boxGeom->m_halfExtents.Y * 2.0f;

			position -= ( right + forward + up ) / 2.0f;

			OrientedBox box = OrientedBox( position, forward, right, up );
			box.RenderSolid( *m_physicsRenderHelper );
			box.RenderWireframe( *m_physicsRenderHelper );
		}
		else if( m_geometry->m_shapeType == EPST_Sphere )
		{
			ShapeGeometrySphere* sphereGeom = (ShapeGeometrySphere*)m_geometry.Get();

			Sphere sphere = Sphere( position, sphereGeom->m_radius );
			sphere.RenderSolid( *m_physicsRenderHelper );
			sphere.RenderWireframe( *m_physicsRenderHelper );
		}
		else if( m_geometry->m_shapeType == EPST_Capsule )
		{
			ShapeGeometryCapsule* capsuleGeom = (ShapeGeometryCapsule*)m_geometry.Get();

			position.Z -= capsuleGeom->m_radius;
			position.Z -= ( capsuleGeom->m_height / 2.0f ); // Engine capsule position is at it's base, physics in the middle
			FixedCapsule capsule = FixedCapsule( position, capsuleGeom->m_radius, capsuleGeom->m_height );
			capsule.RenderSolid( *m_physicsRenderHelper );
			capsule.RenderWireframe( *m_physicsRenderHelper );
		}
		else if( m_geometry->m_shapeType == EPST_Convex )
		{
			ShapeGeometryConvex* convexGeom = (ShapeGeometryConvex*)m_geometry.Get();
			m_physicsRenderHelper->AddCustomMesh( convexGeom->m_vertexPositions, convexGeom->m_indices );
			m_physicsRenderHelper->AddCustomWireframeMesh( convexGeom->m_vertexPositions, convexGeom->m_indices, FragmentBuilderRenderMode::TrueWireframe );

		}
		else if( m_geometry->m_shapeType == EPST_TriMesh )
		{
			ShapeGeometryTriMesh* triMeshGeom = (ShapeGeometryTriMesh*)m_geometry.Get();

			if( IsFlagSet( EPSF_IsMaterialRenderingEnabled ) )
			{
				red::DynArray< Uint16> indices;
				red::DynArray< DebugVertex> vertices;
				triMeshGeom->GetRenderingData( vertices, indices );
				m_physicsRenderHelper->AddCustomMesh( vertices, indices, true );
				m_physicsRenderHelper->AddCustomWireframeMesh( vertices, indices, false, FragmentBuilderRenderMode::TrueWireframe );
			}
			else
			{
				m_physicsRenderHelper->AddCustomMesh( triMeshGeom->m_vertices, triMeshGeom->m_indices, true );
				m_physicsRenderHelper->AddCustomWireframeMesh( triMeshGeom->m_vertices, triMeshGeom->m_indices, false, FragmentBuilderRenderMode::TrueWireframe );
			}
		}

		m_physicsRenderHelper->PrepareForRendering();

		SetFlag( EPBF_RequestRenderingRebuild, false );
	}

	void PhysicsShape::ToggleSelectionVisualization( const Bool drawAsSelected )
	{
		PhysicsBase::ToggleSelectionVisualization( drawAsSelected );
	}

	PhysicsActorPtr PhysicsShape::GetActorParent() const
	{
		if( auto editorScene = GetEditorScene() )
		{
			return red::StaticCast< PhysicsActor >( editorScene->FindByPhysicsId( m_parentId ) );
		}
		return nullptr;
	}

	void PhysicsShape::SetParentId( PhysicsBaseID id )
	{
		m_parentId = id;
		OnParentChanged();
	}

	void PhysicsShape::SetShapeGeometry( ShapeGeometryPtr geo )
	{
		m_geometry = geo;
		if( m_geometry )
		{
			if( m_geometry->m_shapeType == EPST_Box )
			{
				ShapeGeometryBox* boxGeom = (ShapeGeometryBox*)m_geometry.Get();
				m_extractedScale = boxGeom->m_halfExtents;
			}
			else if( m_geometry->m_shapeType == EPST_Sphere )
			{
				ShapeGeometrySphere* sphereGeom = (ShapeGeometrySphere*)m_geometry.Get();
				m_extractedScale = Vector3( sphereGeom->m_radius );
			}
			else if( m_geometry->m_shapeType == EPST_Capsule )
			{
				ShapeGeometryCapsule* capsuleGeom = (ShapeGeometryCapsule*)m_geometry.Get();
				m_extractedScale = Vector3( capsuleGeom->m_radius, capsuleGeom->m_radius, capsuleGeom->m_height );
			}

			UpdateBounds();
		}		
	}

	red::SharedPtr< tools::IEditableProxy > PhysicsShape::CreateEditableProxy( Bool getParentProxy ) const
	{
		if( getParentProxy )
		{
			if( auto parent = GetActorParent() )
			{
				return parent->CreateEditableProxy();
			}
		}
		return PhysicsBase::CreateEditableProxy();
	}


	Bool PhysicsShape::HasFilterData() const
	{
		return m_filterData != nullptr;
	}

	void PhysicsShape::SetIsImported( Bool isImported )
	{
		SetFlag( EPSF_IsImported, isImported );
	}

	Bool PhysicsShape::IsImported() const
	{
		return IsFlagSet( EPSF_IsImported );
	}

	void PhysicsShape::OnSerialize( class IFile& file )
	{
		PhysicsBase::OnSerialize( file );

		if( file.IsReader() )
		{
			Uint8 type;
			file << type;

			switch( type )
			{
				case physics::EPST_Box:
					m_geometry = ShapeGeometryPtr( RED_NEW( ShapeGeometryBox )( ) );
					break;
				case physics::EPST_Sphere:
					m_geometry = ShapeGeometryPtr( RED_NEW( ShapeGeometrySphere )( ) );
					break;
				case physics::EPST_Capsule:
					m_geometry = ShapeGeometryPtr( RED_NEW( ShapeGeometryCapsule )( ) );
					break;
				case physics::EPST_Convex:
					m_geometry = ShapeGeometryPtr( RED_NEW( ShapeGeometryConvex )( ) );
					break;
				case physics::EPST_TriMesh:
					m_geometry = ShapeGeometryPtr( RED_NEW( ShapeGeometryTriMesh )( ) );
					break;
				case physics::EPST_Invalid:
				case physics::EPST_Max:
				default:
					break;
			}
			if( m_geometry )
				m_geometry->OnSerialize( file );
		}
		else if( file.IsWriter() )
		{
			if( m_geometry )
			{
				Uint8 type = (Uint8)m_geometry->m_shapeType;
				file << type;
				m_geometry->OnSerialize( file );
			}
		}
	}

	void PhysicsShape::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		PhysicsBase::OnPropertyPostChange( path, oldValue, value );

		CName propName;
		if( rtti::PathParser( path ).EatName( propName ) )
		{
			if( propName == RED_NAME_CONSTEXPR_NOREG( "parentId" ) )
			{
				OnParentChanged();
			}
			else if( propName == RED_NAME_CONSTEXPR_NOREG( "localToWorld" ) )
			{
				m_localTransform = m_localToWorld * m_parentL2WInverted;
				NotifyPropertyChanged(rtti::AccessPath()[ "localTransform" ]);
			}
			else if( propName == RED_NAME_CONSTEXPR_NOREG( "volumeModifier" ) || propName == RED_NAME_CONSTEXPR_NOREG( "material" ) )
			{
				GetEditorScene()->RecomputeCOM( { GetActorParent()->GetEditorObjectID() } );
			}
		}
	}

	void PhysicsShape::SetupInterop( PhysicsShape* ptr, ::interop::utils::Object& op )
	{}

	Bool PhysicsShape::ExtractParentID( const PhysicsBaseID shapeId, PhysicsBaseID& parentId )
	{
		PhysicsBaseID extracted = shapeId << 48;
		extracted = extracted >> 48;

		if( extracted > 0 )
		{
			parentId = extracted;
			return true;
		}
		return false;
	}

	String PhysicsShape::GetName() const
	{
		const auto& parent = GetActorParent();
		String name = String::EMPTY();
		if( parent )
		{
			name = String::Printf( "[%d] ", parent->FindShapeIndexByPhysicsId( GetPhysicsObjectID() ) );
			physics::ShapeGeometryPtr collider = GetShapeGeometry();
			if( collider->IsA< physics::ShapeGeometryBox >() )
				name += "Box";
			else if( collider->IsA< physics::ShapeGeometrySphere >() )
				name += "Sphere";
			else if( collider->IsA< physics::ShapeGeometryCapsule >() )
				name += "Capsule";
			else if( collider->IsA< physics::ShapeGeometryConvex >() )
				name += "Convex";
			else if( THandle<physics::ShapeGeometryTriMesh> trimesh = Cast< physics::ShapeGeometryTriMesh >( collider ) )
				name += String::Printf( "TriMesh - [%d triangles]", trimesh->m_numTriangles );
			else
				return "Unknown";

			if( IsImported() )
			{
				name += " (imported)";
			}
			else if( m_isObstacle )
			{
				name += " (obstacle)";
			}

			name += " - ";
			if( m_filterData )
			{
				const CName& preset = m_filterData->GetPreset();
				if( preset == RED_NAME_CONSTEXPR_NOREG( "Custom" ) )
				{
					auto& resource = physics::GetFilterResource();
					auto names = resource.GetCollisionGroupsFromMask( m_filterData->GetSimulationFilter().GetTypeMask() );

					for( Uint32 i = 0; i < names.Size(); ++i )
					{
						const CName& typeName = names[ i ];
						if( i > 0 )
							name += " / ";
						name += typeName.AsChar();
					}
				}
				else
				{
					name += preset.AsChar();
				}
			}
			else
			{
				name += " None ";
			}
		}
		return name;
	}

	Color PhysicsShape::GetColorFromParent()
	{
		Color color = Color::WHITE();
		PhysicsActorPtr actorParent = GetActorParent();

		if( actorParent )
		{
			switch( actorParent->GetSimulationType() )
			{
				case EPS_Static:
					color = Color::MAGENTA( m_alpha );
					break;
				case EPS_Dynamic:
					color = Color::GREEN( m_alpha );
					break;
				case EPS_Kinematic:
					color = Color::BLUE( m_alpha );
					break;
				default:
					break;
			}
		}
		return color;
	}
}
