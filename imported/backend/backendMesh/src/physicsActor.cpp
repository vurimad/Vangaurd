/* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.*/

#include "build.h"
#include "physicsActor.h"
#include "physicsShape.h"
#include "physicsMesh.h"
#include "physicsJoint.h"
#include "physicsEditorScene.h"
#include "physicsBackendHelper.h"
#include "meshStructureNodes.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/redReflection/include/mathTetrahedron.h"
#include "../../../common/renderData/include/drawer.h"

// PhysicsActorBoneMapping

RTTI_BEGIN_TYPE_IN_NAMESPACE( EditorActorCOM, physics );
RTTI_PARENT_TYPE( physics::PhysicsBase );
RTTI_PROPERTY( m_comOffset ).editable();
RTTI_PROPERTY( m_inertia ).editable();
RTTI_PROPERTY( m_volume ).readOnly();
RTTI_PROPERTY( m_mass ).readOnly();
RTTI_PROPERTY(m_parentLocalToWorld)	;
RTTI_END_TYPE();

// PhysicsActorBoneMapping

RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsMeshBoneMapping, physics );
RTTI_PROPERTY( m_boneName ).readOnly();
RTTI_PROPERTY( m_meshId ).readOnly();
RTTI_PROPERTY( m_boneIndex ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_ENUM_IN_NAMESPACE( EPhysicsSimulation, physics )
RTTI_ENUM_OPTION_ALIAS( "Static", EPS_Static );
RTTI_ENUM_OPTION_ALIAS( "Dynamic", EPS_Dynamic );
RTTI_ENUM_OPTION_ALIAS( "Kinematic", EPS_Kinematic );
RTTI_ENUM_OPTION_ALIAS( "Undefined", EPS_Undefined );
RTTI_END_ENUM()

RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsActor, physics );
RTTI_PARENT_TYPE( physics::PhysicsBase );
RTTI_PROPERTY( m_actorCOM );
RTTI_PROPERTY_CATEGORY( "2. General" );
RTTI_PROPERTY( m_shapes );
RTTI_PROPERTY( m_simType ).editable().tooltip( "Simulation type of the actor." );
RTTI_PROPERTY( m_meshBoneMap ).editable().customEditor( "PhysicsBoneNameSelector" ).tooltip( "The mesh and bone this actor is mapped to." );
RTTI_PROPERTY_CATEGORY( "3. Advanced" );
RTTI_PROPERTY( m_linearDamping ).editable().range( 0.0f, 1.0f ).tooltip( "Damping coefficient for linear velocity." );
RTTI_PROPERTY( m_angularDamping ).editable().range( 0.0f, 1.0f ).tooltip( "Damping coefficient for angular velocity." );
RTTI_PROPERTY( m_solverIterationsCountPosition ).editable().range( 1, 128 ).tooltip( "Please leave default unless the body behaves inaccurately! Number of position iterations the solver should perform for this body." );
RTTI_PROPERTY( m_solverIterationsCountVelocity ).editable().range( 1, 128 ).tooltip( "Please leave default unless the body behaves inaccurately! Number of velocity iterations the solver should perform for this body." );
RTTI_PROPERTY( m_maxAngularVelocity ).editable().tooltip( "Sets a limit on body angular velocity." );
RTTI_PROPERTY( m_maxDepenetrationVelocity ).editable().tooltip( "This value controls how much velocity the solver can introduce to correct for penetrations in contacts." );
RTTI_PROPERTY( m_maxContactImpulse ).editable().tooltip( "The maximum impulse at a contact between two dynamic or kinematic bodies will be the minimum of the two limit values. " \
	"A good value for that is objects mass. Setting this on a Kinematic actor that doesn't turn dynamic will result in objects possibly passing through it as if no collision occured." );

RTTI_PROPERTY_CATEGORY( "4. Overrides" );
RTTI_PROPERTY( m_isQueryOnlyActor ).editable().tooltip( \
	"The actor's not simulated, meaning that nothing will collide with it, player will freely pass through it etc.\n" \
	"However, any physical queries will report that as if it was a normal physical actor. \n" \
	"This will override shapes settings." );
RTTI_END_TYPE();

namespace physics
{
	const Float c_minMassForENotation = 10000.0f;

	EditorActorCOM::EditorActorCOM()
		: PhysicsBase( 0, EPBT_COM, nullptr )
		, m_comOffset( Transform::IDENTITY() )
		, m_inertia( 1, 1, 1 )
		, m_isSelected( false )
	{}

	EditorActorCOM::EditorActorCOM( PhysicsEditorScene* editorParent, PhysicsBaseID parentId, const Matrix& parentL2W)
		: PhysicsBase( 0, EPBT_COM, editorParent )
		, m_comOffset( Transform::IDENTITY() )
		, m_inertia( 1, 1, 1 )
		, m_isSelected( false )
		, m_parentLocalToWorld( parentL2W )
	{
		m_physicsBaseId = parentId << 32;		
		CalculateLocalToWorld();
	}

	EditorActorCOM::EditorActorCOM( EditorActorCOM& copyFrom )
		: PhysicsBase( copyFrom.m_physicsBaseId, EPBT_COM, copyFrom.GetEditorScene() )
		, m_comOffset( copyFrom.m_comOffset )
		, m_inertia( copyFrom.m_inertia )
		, m_isSelected( false )
	{
		CalculateLocalToWorld();
	}

	EditorActorCOM::~EditorActorCOM()
	{}

	void EditorActorCOM::OnRender( rend::IDebugDrawer& dd, PhysicsRendererOptionsPtr options )
	{
		if( IsFlagSet( EPBF_SelectionVisualizationEnabled ) && options->m_actorCOM )
		{
			// refresh local to world
			CalculateLocalToWorld();
			const auto o = m_localToWorld.GetTranslation();
			const auto x = m_localToWorld.TransformVector( Vector3::EX() ) * m_actorCOMSize;
			const auto y = m_localToWorld.TransformVector( Vector3::EY() ) * m_actorCOMSize;
			const auto z = m_localToWorld.TransformVector( Vector3::EZ() ) * m_actorCOMSize;
			m_physicsRenderHelper->RenderLine( dd, GetSelectableObject(), Matrix::IDENTITY(), o, o + x, Color::RED(), true );
			m_physicsRenderHelper->RenderLine( dd, GetSelectableObject(), Matrix::IDENTITY(), o, o + y, Color::GREEN(), true );
			m_physicsRenderHelper->RenderLine( dd, GetSelectableObject(), Matrix::IDENTITY(), o, o + z, Color::BLUE(), true );
		}
	}

	void EditorActorCOM::PrepareForRendering( PhysicsRendererOptionsPtr options )
	{
		if( !m_physicsRenderHelper )
		{
			m_physicsRenderHelper = red::CreateUniquePtr< PhysicsShapeRenderer >();
		}
		m_actorCOMSize = options->m_actorCOMSize;
	}

	void EditorActorCOM::SetLocalToWorld(const Matrix& localToWorld)
	{
		Matrix invertedParentL2W = m_parentLocalToWorld.FullInverted();
		m_comOffset = ( localToWorld * invertedParentL2W).ToXform();
		CalculateLocalToWorld();
	}

	void EditorActorCOM::CalculateLocalToWorld()
	{
		const auto comToLocal = m_comOffset.ToMatrix();
		Matrix matrix = comToLocal * m_parentLocalToWorld;
		m_localToWorld = matrix;
	}

	void EditorActorCOM::OnParentLocalToWorldChanged(const Matrix& localToWorld)
	{
		m_parentLocalToWorld = localToWorld;
		CalculateLocalToWorld();
	}

	String EditorActorCOM::GetName() const
	{
		if( m_mass > c_minMassForENotation )
		{
			return String::Printf( "COM - mass [%.3e]", m_mass );
		}
		else
		{
			return String::Printf( "COM - mass [%.3f]", m_mass );
		}
	}

	physics::PhysicsBaseID EditorActorCOM::ExtractParentID( PhysicsBaseID id )
	{
		return id >> 32;
	}

	void EditorActorCOM::SetupInterop( EditorActorCOM* ptr, ::interop::utils::Object& op )
	{}
	//////////////////////////////////////////////////////////////////////////

	PhysicsMeshBoneMapping::PhysicsMeshBoneMapping()
		: m_meshId( 0 )
		, m_boneName( CName::NONE() )
		, m_boneIndex( -1 )
	{}

	PhysicsMeshBoneMapping::PhysicsMeshBoneMapping( PhysicsBaseID id, CName boneName, Int32 boneIndex )
		: m_meshId( id )
		, m_boneName( boneName )
		, m_boneIndex( boneIndex )
	{}

	PhysicsMeshBoneMapping::~PhysicsMeshBoneMapping()
	{}

	Bool PhysicsMeshBoneMapping::IsValid()
	{
		return m_meshId != 0 && ( m_boneName != CName::NONE() || m_boneIndex >= 0 );
	}

	Bool PhysicsMeshBoneMapping::operator==( PhysicsMeshBoneMapping& mapping )
	{
		return m_meshId == mapping.m_meshId && m_boneName == mapping.m_boneName && m_boneIndex == mapping.m_boneIndex;
	}

	//////////////////////////////////////////////////////////////////////////

	PhysicsActor::PhysicsActor()
		: PhysicsBase( 0, EPBT_Actor, nullptr )
		, m_linearDamping( 0.0f )
		, m_angularDamping( 0.0f )
		, m_actorCOM( )
		, m_solverIterationsCountPosition( 4 )
		, m_solverIterationsCountVelocity( 1 )
		, m_nextShapeId( 1 )
		, m_shapes( red::PoolBackend() )
		, m_actorSize( -1.f )
		, m_isQueryOnlyActor( false )
	{
	}

	PhysicsActor::PhysicsActor( PhysicsBaseID id,PhysicsEditorScene* editorParent )
		: PhysicsBase( id, EPBT_Actor, editorParent )
		, m_linearDamping( 0.0f )
		, m_angularDamping( 0.0f )
		, m_solverIterationsCountPosition( 4 )
		, m_solverIterationsCountVelocity( 1 )
		, m_nextShapeId( 1 )
		, m_shapes( red::PoolBackend() )
		, m_actorSize( -1.f )
		, m_isQueryOnlyActor( false )
	{
		m_actorCOM = CreateHandle<EditorActorCOM>( GetEditorScene(), GetPhysicsObjectID(), GetLocalToWorld() );
	}

	PhysicsActor::PhysicsActor( const THandle< PhysicsActor >& copyFrom )
		: PhysicsBase( copyFrom->GetPhysicsObjectID(), EPBT_Actor, copyFrom->GetEditorScene() )
		, m_linearDamping( copyFrom->m_linearDamping )
		, m_angularDamping( copyFrom->m_angularDamping )
		, m_actorCOM( copyFrom->m_actorCOM )
		, m_simType( copyFrom->m_simType )
		, m_nextShapeId( 1 )
		, m_shapes( red::PoolBackend() )
		, m_actorSize( -1.f )
		, m_isQueryOnlyActor( false )
	{
		m_actorCOM = CreateHandle<EditorActorCOM>( GetEditorScene(), GetPhysicsObjectID(), GetLocalToWorld());
	}

	PhysicsActor::~PhysicsActor()
	{}

	void PhysicsActor::SetLocalToWorld( const Matrix& localToWorld )
	{
		PhysicsBase::SetLocalToWorld( localToWorld );

		NotifyChildrenOnLocalToWorldChanged();
	}

	void PhysicsActor::NotifyChildrenOnLocalToWorldChanged()
	{
		// Update children
		Uint32 size = m_shapes.Size();
		for( Uint32 i = 0; i < size; ++i )
		{
			m_shapes[ i ]->OnParentLocalToWorldChanged(m_localToWorld);
		}

		m_actorCOM->OnParentLocalToWorldChanged(m_localToWorld);
	}

	void PhysicsActor::AddShape(PhysicsShapePtr shape)
	{
		m_shapes.PushBack( shape );
		shape->m_physicsBaseId = m_nextShapeId++;
		shape->OnParentLocalToWorldChanged( m_localToWorld );

		for( const red::WeakPtr<tools::IMeshPhysicsStructureNode>& handlerWeak : m_notificationHandlers )
		{
			if( red::SharedPtr<tools::IMeshPhysicsStructureNode> handler = handlerWeak.Lock() )
			{
				handler->OnPhysicsObjectAdded( shape );
			}
		}
		RecomputeCOM();
	}

	void PhysicsActor::RemoveShape( PhysicsShapePtr shape )
	{
		m_shapes.Remove( shape );
		shape->OnRemove();
		RecomputeCOM();
	}

	void PhysicsActor::OnUpdate()
	{
		PhysicsBase::OnUpdate();
		m_bounds.Clear();
		for( PhysicsShapePtr ptr : m_shapes )
		{
			ptr->OnUpdate();
			m_bounds.AddBox( ptr->GetLocalTransform().TransformBox( ptr->GetBoundsLocal() ) );
		}
	}

	void PhysicsActor::OnRender( rend::IDebugDrawer& dd, PhysicsRendererOptionsPtr options )
	{
		for( PhysicsShapePtr ptr : m_shapes )
		{
			ptr->OnRender( dd, options );
		}	

		m_actorCOM->OnRender( dd, options );

		if( options->m_actorNames )
		{
			const auto o = m_localToWorld.GetTranslation();
			m_physicsRenderHelper->RenderText( dd, o, GetName(), Color::YELLOW(), true );
		}

		if( options->m_actors )
		{
			// Update rendering transform to ignore rotation
			Matrix localToWorld = Matrix::IDENTITY();
			localToWorld.SetTranslation( GetLocalToWorld().GetTranslation() );
			m_physicsRenderHelper->SetLocalToWorld( localToWorld );
			m_physicsRenderHelper->SetOverlay( true );

			Color color = Color::ORANGE( m_alpha );
			switch( GetSimulationType() )
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
			m_physicsRenderHelper->SetColor( color );
			PhysicsBase::OnRender( dd, options );
		}		
	}

	void PhysicsActor::OnPostLoad( const PostLoadContext& context )
	{
		PhysicsBase::OnPostLoad( context );

		for( PhysicsShapePtr ptr : m_shapes )
		{
			ptr->SetParentId( GetPhysicsObjectID() );
			ptr->OnPostLoad( context );
		}

		if( !m_actorCOM || m_actorCOM->m_mass <= 0.f )
		{
			RecomputeCOM();
		}
	}

	void PhysicsActor::OnRestore(PhysicsEditorScene* scene )
	{
		PhysicsBase::OnRestore( scene );

		for( PhysicsShapePtr ptr : m_shapes )
		{
			ptr->OnRestore( scene );
		}

		if( !m_actorCOM )
		{
			RecomputeCOM();
		}
		else
		{
			m_actorCOM->OnRestore( scene );
		}
	}

	void PhysicsActor::PrepareForRendering( PhysicsRendererOptionsPtr options )
	{
		PhysicsBase::PrepareForRendering( options );
		m_alpha = (Uint8)(options->m_actorAlpha * 255);
		if( IsFlagSet( EPBF_RequestRenderingRebuild ) || m_actorSize != options->m_actorSize )
		{
			m_actorSize = options->m_actorSize;
			if( !m_physicsRenderHelper )
			{
				m_physicsRenderHelper = red::CreateUniquePtr< PhysicsShapeRenderer >( Color::RED() );
			}
			m_physicsRenderHelper->Reset();
			Float xOffset = m_actorSize;
			Float yOffset = 1.73f * xOffset; // sqrt( 3 ) * xOffset
			Float centreYOffset = 0.33f * yOffset;

			Vector4 centreOffset = -(Vector3::EY() + Vector3::EZ()) * centreYOffset;
			Vector4 left = centreOffset + Vector4::EX() * xOffset;
			Vector4 right = centreOffset - Vector4::EX() * xOffset;
			Vector4 up = (Vector4::EZ() * (yOffset - centreYOffset));
			Vector4 front = centreOffset + Vector4::EY() * yOffset;
			Tetrahedron tetra = Tetrahedron( left, up, right,  front);
			tetra.RenderSolid( *m_physicsRenderHelper );
			tetra.RenderWireframe( *m_physicsRenderHelper );
			m_physicsRenderHelper->PrepareForRendering();

			SetFlag( EPBF_RequestRenderingRebuild, false );
		}

		for( PhysicsShapePtr ptr : m_shapes )
		{
			ptr->PrepareForRendering( options );
		}
		m_actorCOM->PrepareForRendering( options );
	}

	void PhysicsActor::ToggleSelectionVisualization( const Bool drawAsSelected )
	{
		for( PhysicsShapePtr ptr : m_shapes )
		{
			ptr->ToggleSelectionVisualization( drawAsSelected );
		}
		PhysicsBase::ToggleSelectionVisualization( drawAsSelected );
	}

	PhysicsBasePtr PhysicsActor::FindShapeByGlobalID( const tools::EditorObjectID globalId )
	{
		for( PhysicsShapePtr physObj : m_shapes )
		{
			if( physObj->GetEditorObjectID() == globalId )
			{
				return physObj;
			}
		}
		return nullptr;
	}

	physics::PhysicsBasePtr PhysicsActor::FindShapeByPhysicsId( const PhysicsBaseID physicsId )
	{
		for( PhysicsShapePtr physObj : m_shapes )
		{
			if( physObj->GetPhysicsObjectID() == physicsId )
			{
				return physObj;
			}
		}
		return nullptr;
	}

	Uint32 PhysicsActor::FindShapeIndexByPhysicsId( const PhysicsBaseID physicsId )
	{	
		for( Uint32 index : m_shapes.Indices() )
		{
			const PhysicsShapePtr& physObj = m_shapes[ index ];
			if( physObj->GetPhysicsObjectID() == physicsId )
			{
				return index;
			}
		}
		return UINT32_MAX;
	}

	Bool PhysicsActor::OnPropertyPreChange( const rtti::AccessPath& path, red::SharedPtr<const rtti::ValueHolder>& value )
	{
		PhysicsBase::OnPropertyPreChange( path, value );

		CName propName;
		if( rtti::PathParser( path ).EatName( propName ) )
		{
			if( propName == RED_NAME_CONSTEXPR_NOREG( "meshBoneMap" ) )
			{
				if( auto editorScene = GetEditorScene() )
				{
					PhysicsMeshBoneMapping data;
					GetTypeObject<PhysicsMeshBoneMapping>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *value );
					editorScene->BindPhysicsActorToMesh( m_physicsBaseId, data.m_meshId, data.m_boneName );
					SnapToBone();

					for( const red::WeakPtr<tools::IMeshPhysicsStructureNode>& handlerWeak : m_notificationHandlers )
					{
						if( red::SharedPtr<tools::IMeshPhysicsStructureNode> handler = handlerWeak.Lock() )
						{
							handler->OnPhysicsObjectPropertyChanged( HandleFromThis<PhysicsActor>(), path );
						}
					}
					return true;
				}
			}
		}
		return true;
	}

	void PhysicsActor::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		PhysicsBase::OnPropertyPostChange(path, oldValue, value);

		CName propName;
		if( rtti::PathParser(path).EatName(propName) )
		{
			if( propName == RED_NAME_CONSTEXPR_NOREG( "localToWorld" ) )
			{
				 NotifyChildrenOnLocalToWorldChanged();
			}
		}
	}

	Bool PhysicsActor::IsValid()
	{
		if( !m_localToWorld.IsOk() )
		{
			return false;
		}
		if( !m_meshBoneMap.IsValid() )
		{
			return false;
		}
		return true;
	}

	void PhysicsActor::ClearBoneMapping()
	{
		m_meshBoneMap = PhysicsMeshBoneMapping();

		for( const red::WeakPtr<tools::IMeshPhysicsStructureNode>& handlerWeak : m_notificationHandlers )
		{
			if( red::SharedPtr<tools::IMeshPhysicsStructureNode> handler = handlerWeak.Lock() )
			{
				handler->OnPhysicsObjectPropertyChanged( HandleFromThis<PhysicsActor>(), rtti::AccessPath( "meshBoneMap" ) );
			}
		}
	}

	void PhysicsActor::ClearActorShapes()
	{
		for( auto& shape : m_shapes )
		{
			if( shape )
				shape->OnRemove();
		}
		m_shapes.Clear();
	}

	void PhysicsActor::OnRemove()
	{
		if( m_meshBoneMap.IsValid()  )
		{
			if( auto editorScene = GetEditorScene() )
			{
				PhysicsMeshPtr meshPtr = Cast<PhysicsMesh>( editorScene->FindByPhysicsId( m_meshBoneMap.m_meshId ) );
				if( meshPtr )
				{
					meshPtr->RemoveBoneBinding( m_meshBoneMap.m_boneName, m_meshBoneMap.m_boneIndex );
				}
			}			
		}
	}

	Bool PhysicsActor::RecomputeCOM()
	{
		if( !m_actorCOM )
		{
			m_actorCOM = CreateHandle<EditorActorCOM>( GetEditorScene(), GetPhysicsObjectID(), GetLocalToWorld());
		}

		Bool result = physics::PhysicsBackendHelper::ComputeCOMForEditorActor( HandleFromThis<PhysicsActor>() );

		for( const red::WeakPtr<tools::IMeshPhysicsStructureNode>& handlerWeak : m_notificationHandlers )
		{
			if( red::SharedPtr<tools::IMeshPhysicsStructureNode> handler = handlerWeak.Lock() )
			{
				handler->OnPhysicsObjectPropertyChanged( HandleFromThis<PhysicsActor>(), rtti::AccessPath( "actorCOM" ) );
			}
		}

		return result;
	}

	void PhysicsActor::SnapToBone()
	{
		if( auto editorScene = GetEditorScene() )
		{
			PhysicsMeshPtr meshPtr = Cast<PhysicsMesh>( editorScene->FindByPhysicsId( m_meshBoneMap.m_meshId ) );
			if( meshPtr )
			{
				SPhysicsActorBoneMapping boneMap;
				if( meshPtr->GetActorBinding( m_meshBoneMap.m_boneIndex, boneMap ) )
				{
					const Matrix rigMat = meshPtr->GetBoneRigMatrix( m_meshBoneMap.m_boneIndex );
					Transform rigTransform = Transform( rigMat.GetTranslation(), rigMat.ToQuat() );
					Matrix actorL2W = rigTransform.ToInvMatrix() * meshPtr->GetLocalToWorld();
					meshPtr->BindPhysicsActor( GetPhysicsObjectID(), rigTransform.ToMatrix(), Matrix::IDENTITY(), m_meshBoneMap.m_boneName, m_meshBoneMap.m_boneIndex );
					SetLocalToWorld( actorL2W );
				}
			}
		}
	}

	String PhysicsActor::GetName() const
	{
		String output = String::Printf( "Actor (ID: %d, ", GetPhysicsObjectID() );
		if( auto editorScene = GetEditorScene() )
		{
			if( !editorScene->IsSingleMeshScene() )
			{
				output += String::Printf( "MeshID: %d, ", m_meshBoneMap.m_meshId );
			}
		}

		if( m_actorCOM->m_mass > c_minMassForENotation )
		{
			output += String::Printf( "Mass: %.3e, ", m_actorCOM->m_mass );
		}
		else
		{
			output += String::Printf( "Mass: %.3f, ", m_actorCOM->m_mass );
		}

		output += String::Printf( "bone: %hs )", m_meshBoneMap.m_boneName.AsChar() );

		return output;
	}

	void PhysicsActor::EvaluateSerializationDependencies( red::DynArray<THandle<ISerializable>>& dependenciesArray )
	{
		if( auto editorScene = GetEditorScene() )
		{
			PhysicsMeshPtr meshPtr = Cast<PhysicsMesh>( editorScene->FindByPhysicsId( m_meshBoneMap.m_meshId ) );
			if( meshPtr )
			{
				dependenciesArray.PushBack( meshPtr );
			}

			const auto& objectsMap = editorScene->GetPhysicsObjectsMap();
			PhysicsBaseID actorId = GetPhysicsObjectID();
			for( const auto it : objectsMap )
			{
				const auto& object = it.Value();
				if( object->GetBaseType() == EPBT_Joint )
				{
					const PhysicsJointPtr jointPtr = Cast<PhysicsJoint>( object );
					if( jointPtr->GetActorId( EPJAI_Child ) == actorId || jointPtr->GetActorId( EPJAI_Parent ) == actorId )
					{
						dependenciesArray.PushBack( jointPtr );
					}
				}
			}
		}
	}

	void PhysicsActor::SetupInterop( PhysicsActor* ptr, ::interop::utils::Object& op )
	{}
}
