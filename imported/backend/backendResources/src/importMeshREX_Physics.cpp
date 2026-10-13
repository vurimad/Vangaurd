#include "build.h"
#include "importMeshREX_Common.h"
#include "importMeshREX_Physics.h"


#include "../../../common/redContainers/include/queue.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"

#include "../../../common/resourceMesh/include/meshParamPhysics.h"

#include "../../../common/physics/include/physicsSceneStructs.h"
#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemBody.h"
#include "../../../common/physics/include/physicsSystemJoint.h"


#include "../../../backend/backendMesh/include/meshDataView.h"
#include "../../../backend/backendMesh/include/meshDataBuilder.h"
#include "../../../backend/backendMesh/include/meshPhysicsUtils.h"

#include "../../../backend/backendREA/include/reCommonUtils.h"


#define PHYSICAL_MATERIALS_DIRECTORY "base\\physics\\"


static const String STR_COLL( "_COLL" );

void SetCollisionFilterData( const ReFileBaseCollisionNode& collisionNode, physics::ICollider& collisionItem )
{
	if( collisionNode.getCollisionPresetName() != "" )
	{
		physics::FilterDataPtr filterData = CreateHandle< physics::FilterData >();
		CName presetName = RED_NAME( collisionNode.getCollisionPresetName() );
		filterData->ApplyPreset( presetName );
		if( collisionNode.getCollisionPresetName() == "Custom" )
		{
			physics::SimulationFilter simulationFilter;

			physics::QueryFilter queryFilter;

			for( auto& collisionChannel : collisionNode.getCollisionChannelFlags() )
			{
				// Collision info.
				if( collisionChannel.second.Flags.IsType )
				{
					simulationFilter.AddType( RED_NAME( collisionChannel.first ) );
				}
				if( collisionChannel.second.Flags.CollidesWith )
				{
					simulationFilter.AddMask( RED_NAME( collisionChannel.first ) );
				}

				// Query info.
				const Bool deprecated_blocks = collisionChannel.second.Flags.QueryFlag == ReFileBaseCollisionNode::ReCollisionChannelFlags::ReCollisionChannelQueryFlag::Blocks;
				const Bool deprecated_touches = collisionChannel.second.Flags.QueryFlag == ReFileBaseCollisionNode::ReCollisionChannelFlags::ReCollisionChannelQueryFlag::Touches;

				if( deprecated_blocks || deprecated_touches )
				{
					queryFilter.AddGroup( RED_NAME( collisionChannel.first ) );
				}
			}
			filterData->Build( queryFilter, simulationFilter );
		}

		collisionItem.m_filterData = filterData;
	}

	if( collisionNode.getPhysicalMaterial() != "" && collisionNode.getPhysicalMaterial() != "default" )
	{
		red::String name = red::String( PHYSICAL_MATERIALS_DIRECTORY ) + red::String( collisionNode.getPhysicalMaterial().getData() ) + red::String( ".physmat" );
		TResRef< physics::MaterialResource > materialResource( res::ResourcePath::Build( red::StringView( name ) ) );
		if( materialResource.Get() )
		{
			collisionItem.m_material = materialResource->GetMaterialName();
		}
	}
}


void SetCollisionCommonAttributes( std::shared_ptr<ReFileBaseCollisionNode> collisionNode, physics::ICollider& collisionItem )
{
	if (collisionNode->nodeFlags().hasFlag("isQueryShapeOnly"))
	{
		collisionItem.m_isQueryShapeOnly = collisionNode->nodeFlags().getFlag( "isQueryShapeOnly" );
	}

	if (collisionNode->hasAttr("volumeModifier"))
	{
		collisionItem.m_volumeModifier = (Float)collisionNode->getAttr( "volumeModifier" );
	}
}


THandle<physics::ColliderConvex> ReConvexToColliderConvexShape( ReFileCollisionConvexPtr convexCollision, String& warnings )
{
	String collisionName( convexCollision->getCollisionName().getData() );

	Int32 numv = convexCollision->getNumVertices();
	Int32 numf = convexCollision->getNumFaces();
	INT32 numMat = convexCollision->getNumMaterials();

	Matrix pose(
		Vector4( convexCollision->mTransformMatrix[ 0 ], convexCollision->mTransformMatrix[ 1 ], convexCollision->mTransformMatrix[ 2 ], 0.0f ),
		Vector4( convexCollision->mTransformMatrix[ 3 ], convexCollision->mTransformMatrix[ 4 ], convexCollision->mTransformMatrix[ 5 ], 0.0f ),
		Vector4( convexCollision->mTransformMatrix[ 6 ], convexCollision->mTransformMatrix[ 7 ], convexCollision->mTransformMatrix[ 8 ], 0.0f ),
		Vector4( convexCollision->mTransformMatrix[ 9 ] * c_fromReScale, convexCollision->mTransformMatrix[ 10 ] * c_fromReScale, convexCollision->mTransformMatrix[ 11 ] * c_fromReScale ) );
	pose[ 0 ].Normalize3();
	pose[ 1 ].Normalize3();
	pose[ 2 ].Normalize3();

	String materialName( convexCollision->mMaterialName.getData() );
	if (materialName.Empty())
	{
		materialName = convexCollision->getPhysicalMaterial();
	}
	materialName.ToLower();
	CName material = RED_NAME( materialName );

	Vector3* c_fromReScaled_positions = RED_NEW_ARRAY( Vector3, convexCollision->getNumVertices(), red::PoolBackend );

	for( int i = 0; i < convexCollision->getNumVertices(); i++ )
	{
		c_fromReScaled_positions[ i ].X = convexCollision->mPositions[ 3 * i + 0 ] * c_fromReScale;
		c_fromReScaled_positions[ i ].Y = convexCollision->mPositions[ 3 * i + 1 ] * c_fromReScale;
		c_fromReScaled_positions[ i ].Z = convexCollision->mPositions[ 3 * i + 2 ] * c_fromReScale;
	}

	auto shape = physics::ColliderConvex::Create( c_fromReScaled_positions, convexCollision->getNumVertices() );
	if( !shape )
	{
		warnings += String::Printf( "ImportConvex: Creating convex collider failed: %hs - check log for more details.", collisionName.AsChar() );
		RED_LOG_ERROR( "ImportConvex: Creating convex collider failed: %hs.", collisionName.AsChar() );
		return nullptr;
	}

	shape->m_isImported = true;
	shape->m_localToBody = Transform( pose.GetTranslation(), pose.ToQuat() );
	shape->m_material = material;

	SetCollisionFilterData( *convexCollision, *shape );
	SetCollisionCommonAttributes( convexCollision, *shape );

	RED_DELETE_ARRAY( c_fromReScaled_positions, convexCollision->getNumVertices() * 3, red::PoolBackend );
	return shape;
}


String ImportCollisionConvexes( const std::vector< ReFileCollisionConvexPtr >& convexCollisions, red::DynArray< CollShapeImportData >& collisionItems )
{
	String warnings = String::EMPTY();
	Uint32 convexCollisionSize = (Uint32)convexCollisions.size();
	for( auto& convexCollision: convexCollisions )
	{
		THandle<physics::ColliderConvex> shape = ReConvexToColliderConvexShape( convexCollision, warnings );
		
		if( shape )
		{
			CollShapeImportData data;
			data.m_name = RED_NAME( convexCollision->getCollisionName().getData() );
			data.m_shape = shape;
			data.m_simulationType = ( physics::SimulationType )convexCollision->getPhysicsMode();
			collisionItems.PushBack( data );
		}
	}
	return warnings;
}


THandle<physics::ColliderBox> ReBoxToColliderBoxShape( ReFileCollisionBoxPtr box )
{
	Matrix pose(
		Vector4( box->mTransformMatrix[ 0 ], box->mTransformMatrix[ 1 ], box->mTransformMatrix[ 2 ], 0.0f ),
		Vector4( box->mTransformMatrix[ 3 ], box->mTransformMatrix[ 4 ], box->mTransformMatrix[ 5 ], 0.0f ),
		Vector4( box->mTransformMatrix[ 6 ], box->mTransformMatrix[ 7 ], box->mTransformMatrix[ 8 ], 0.0f ),
		Vector4( box->mTransformMatrix[ 9 ] * c_fromReScale, box->mTransformMatrix[ 10 ] * c_fromReScale, box->mTransformMatrix[ 11 ] * c_fromReScale ) );
	pose[ 0 ].Normalize3();
	pose[ 1 ].Normalize3();
	pose[ 2 ].Normalize3();
	Vector4 halfExtends = Vector4( box->getLength() * c_fromReScale, box->getWidth() * c_fromReScale, box->getHeight() * c_fromReScale ) / 2.f;

	String collisionName( box->getCollisionName().getData() );

	String materialName( box->getMaterialName().getData() );
	if (materialName.Empty())
	{
		materialName = box->getPhysicalMaterial();
	}
	materialName.ToLower();

	auto shape = physics::ColliderBox::Create( halfExtends );
	if( !shape )
		return nullptr;

	shape->m_isImported = true;
	shape->m_localToBody = Transform( pose.GetTranslation(), pose.ToQuat() );
	shape->m_material = RED_NAME( materialName );

	SetCollisionFilterData( *box, *shape );
	SetCollisionCommonAttributes( box, *shape );

	return shape;
}


String ImportCollisionBoxes( const std::vector< ReFileCollisionBoxPtr >& boxes, red::DynArray< CollShapeImportData >& collisionItems )
{
	String warnings = String::EMPTY();
	for( auto box : boxes )
	{
		THandle<physics::ColliderBox> shape = ReBoxToColliderBoxShape( box );
		if( shape )
		{
			CollShapeImportData data;
			data.m_name = RED_NAME( box->getCollisionName().getData() );
			data.m_shape = shape;
			data.m_simulationType = ( physics::SimulationType )box->getPhysicsMode();
			collisionItems.PushBack( data );
		}
		else
		{
			warnings += String::Printf( "Box collider %hs failed to generate - check its dimensions", box->getCollisionName().getData() );
		}
	}
	return warnings;
}


THandle<physics::ColliderSphere> ReSphereToColliderSphere( const ReFileCollisionSpherePtr sphere )
{
	Matrix pose(
		Vector4( sphere->mTransformMatrix[ 0 ], sphere->mTransformMatrix[ 1 ], sphere->mTransformMatrix[ 2 ], 0.0f ),
		Vector4( sphere->mTransformMatrix[ 3 ], sphere->mTransformMatrix[ 4 ], sphere->mTransformMatrix[ 5 ], 0.0f ),
		Vector4( sphere->mTransformMatrix[ 6 ], sphere->mTransformMatrix[ 7 ], sphere->mTransformMatrix[ 8 ], 0.0f ),
		Vector4( sphere->mTransformMatrix[ 9 ] * c_fromReScale, sphere->mTransformMatrix[ 10 ] * c_fromReScale, sphere->mTransformMatrix[ 11 ] * c_fromReScale ) );
	pose[ 0 ].Normalize3();
	pose[ 1 ].Normalize3();
	pose[ 2 ].Normalize3();

	String materialName( sphere->getMaterialName().getData() );
	if (materialName.Empty())
	{
		materialName = sphere->getPhysicalMaterial();
	}
	materialName.ToLower();

	auto shape = physics::ColliderSphere::Create( sphere->getRadius() * c_fromReScale );
	if( !shape )
		return nullptr;

	shape->m_isImported = true;
	shape->m_localToBody = Transform( pose.GetTranslation(), pose.ToQuat() );
	shape->m_material = RED_NAME( materialName );

	SetCollisionFilterData( *sphere, *shape );
	SetCollisionCommonAttributes( sphere, *shape );

	return shape;
}


String ImportCollisionSpheres( const std::vector< ReFileCollisionSpherePtr >& spheres, red::DynArray< CollShapeImportData >& collisionItems )
{
	String warnings = String::EMPTY();
	for( const auto sphere : spheres )
	{
		String collisionName( sphere->getCollisionName().getData() );

		THandle<physics::ColliderSphere> shape = ReSphereToColliderSphere( sphere );

		if( shape )
		{
			CollShapeImportData data;
			data.m_name = RED_NAME( collisionName );
			data.m_shape = shape;
			data.m_simulationType = ( physics::SimulationType )sphere->getPhysicsMode();
			collisionItems.PushBack( data );
		}	
		else
		{
			warnings += String::Printf( "Sphere collider %hs failed to generate - check its radius", collisionName.AsChar() );
		}
	}
	return warnings;
}


THandle<physics::ColliderCapsule> ReCapsuleToColliderCapsule( ReFileCollisionCapsulePtr capsule )
{
	Matrix pose(
		Vector4( capsule->mTransformMatrix[ 0 ], capsule->mTransformMatrix[ 1 ], capsule->mTransformMatrix[ 2 ], 0.0f ),
		Vector4( capsule->mTransformMatrix[ 3 ], capsule->mTransformMatrix[ 4 ], capsule->mTransformMatrix[ 5 ], 0.0f ),
		Vector4( capsule->mTransformMatrix[ 6 ], capsule->mTransformMatrix[ 7 ], capsule->mTransformMatrix[ 8 ], 0.0f ),
		Vector4( capsule->mTransformMatrix[ 9 ] * c_fromReScale, capsule->mTransformMatrix[ 10 ] * c_fromReScale, capsule->mTransformMatrix[ 11 ] * c_fromReScale ) );
	pose[ 0 ].Normalize3();
	pose[ 1 ].Normalize3();
	pose[ 2 ].Normalize3();

	String materialName( capsule->getMaterialName().getData() );
	if (materialName.Empty())
	{
		materialName = capsule->getPhysicalMaterial();
	}
	materialName.ToLower();

	auto shape = physics::ColliderCapsule::Create( capsule->getRadius() * c_fromReScale, capsule->getHeight() * c_fromReScale );
	if( !shape )
		return nullptr;

	shape->m_isImported = true;
	shape->m_localToBody = Transform( pose.GetTranslation(), pose.ToQuat() );
	shape->m_material = RED_NAME( materialName );

	SetCollisionFilterData( *capsule, *shape );
	SetCollisionCommonAttributes( capsule, *shape );

	return shape;
}

String ImportCollisionCapsules( const std::vector< ReFileCollisionCapsulePtr >& capsules, red::DynArray< CollShapeImportData >& collisionItems )
{
	String warnings = String::EMPTY();
	for( auto capsule : capsules )
	{
		String collisionName( capsule->getCollisionName().getData() );

		THandle<physics::ColliderCapsule> shape = ReCapsuleToColliderCapsule( capsule );

		if( shape )
		{
			CollShapeImportData data;
			data.m_name = RED_NAME( collisionName );
			data.m_shape = shape;
			data.m_simulationType = ( physics::SimulationType )capsule->getPhysicsMode();
			collisionItems.PushBack( data );
		}
		else
		{
			warnings += String::Printf( "Capsule collider %hs failed to generate - check its radius", collisionName.AsChar() );
		}
	}
	return warnings;
}


THandle<physics::ColliderMesh> ReTriMeshToColliderMesh( ReFileTriMeshPtr trimesh, String& warnings )
{
	Matrix pose(
		Vector4( trimesh->mTransformMatrix[ 0 ], trimesh->mTransformMatrix[ 1 ], trimesh->mTransformMatrix[ 2 ], 0.0f ),
		Vector4( trimesh->mTransformMatrix[ 3 ], trimesh->mTransformMatrix[ 4 ], trimesh->mTransformMatrix[ 5 ], 0.0f ),
		Vector4( trimesh->mTransformMatrix[ 6 ], trimesh->mTransformMatrix[ 7 ], trimesh->mTransformMatrix[ 8 ], 0.0f ),
		Vector4( trimesh->mTransformMatrix[ 9 ] * c_fromReScale, trimesh->mTransformMatrix[ 10 ] * c_fromReScale, trimesh->mTransformMatrix[ 11 ] * c_fromReScale ) );
	pose[ 0 ].Normalize3();
	pose[ 1 ].Normalize3();
	pose[ 2 ].Normalize3();

	String materialName( "default" );
	if (!trimesh->hasDataChannel( RE_PHYS_MATERIAL_NAMES_CHANNEL ) || !trimesh->hasDataChannel( RE_PHYS_MATERIAL_IDX_CHANNEL ))
	{
		materialName = trimesh->getPhysicalMaterial();
	}
	materialName.ToLower();

	red::DynArray< Uint16 > indicies;
	red::DynArray< Vector3 > scaled_vertices( trimesh->getNumVertices(), red::PoolBackend() );

	for( int i = 0; i < trimesh->getNumVertices(); i++ )
	{
		auto& v = scaled_vertices[ i ];
		v.X = trimesh->getVertexById( i * 3 + 0 ) * c_fromReScale;
		v.Y = trimesh->getVertexById( i * 3 + 1 ) * c_fromReScale;
		v.Z = trimesh->getVertexById( i * 3 + 2 ) * c_fromReScale;
	}

	for( int i = 0; i < trimesh->getNumFaces(); i++ )
	{
		indicies.PushBack( (Uint16)trimesh->getIndexById( i * 3 + 0 ) );
		indicies.PushBack( (Uint16)trimesh->getIndexById( i * 3 + 1 ) );
		indicies.PushBack( (Uint16)trimesh->getIndexById( i * 3 + 2 ) );
	}

	red::DynArray< Uint16 > removedFaces{red::PoolBackend()};
	auto shape = physics::ColliderMesh::Create( scaled_vertices, indicies, false, removedFaces );

	if( !shape )
	{
		warnings += String::Printf( "ImportConvex: Creating Trimesh collider failed: %ls - check log for more details.", trimesh->getCollisionName().getData() );
		RED_LOG_ERROR( "ImportConvex: Creating Trimesh collider failed: %ls.", trimesh->getCollisionName().getData() );
		return nullptr;
	}

	shape->m_isImported = true;
	shape->m_localToBody = Transform( pose.GetTranslation(), pose.ToQuat() );
	shape->m_material = RED_NAME( materialName );

	SetCollisionFilterData( *trimesh, *shape );
	SetCollisionCommonAttributes( trimesh, *shape );

	// Load per-face materials assignment
	if( trimesh->hasDataChannel( RE_PHYS_MATERIAL_NAMES_CHANNEL ) && trimesh->hasDataChannel( RE_PHYS_MATERIAL_IDX_CHANNEL ) )
	{
		unsigned int size;
		reString* data;
		trimesh->getDataChannel( RE_PHYS_MATERIAL_NAMES_CHANNEL, size, data );
		if( size > 0 )
		{
			shape->m_faceMaterials.Resize( size );
			for( auto i : shape->m_faceMaterials.Indices() )
			{
				auto& material = shape->m_faceMaterials[ i ];
				red::String name = red::String( PHYSICAL_MATERIALS_DIRECTORY ) + red::String( data[ i ].getData() ) + red::String( ".physmat" );
				TResRef< physics::MaterialResource > materialResource( res::ResourcePath::Build( red::StringView( name ) ) );
				if( materialResource.Get() )
				{
					material = materialResource->GetMaterialName();
				}
				else
				{
					warnings += String::Printf( "ImportCollisionTriMeshes: Physical material for triangle not found: %ls", materialResource.GetPath().ToDebugString() );
					RED_LOG_WARNING( "ImportCollisionTriMeshes: Physical material for triangle not found: %ls", materialResource.GetPath().ToDebugString() );
					material = physics::MaterialResource::DEFAULT();
				}
			}
		}

		int* indices;
		trimesh->getDataChannel( RE_PHYS_MATERIAL_IDX_CHANNEL, size, indices );
		auto indicesSpan = red::MakeArraySpan< int >( indices, size );
		if( size > 0 )
		{
			for( Uint32 idx : indicesSpan.Indices() )
			{
				if( std::none_of( begin( removedFaces ), end( removedFaces ), [idx]( auto i ) { return idx == i; } ) )
				{
					shape->m_faceMaterialIndices.PushBack( indices[ idx ] );
				}
			}
		}
	}
	return shape;
}


String ImportCollisionTriMeshes( const std::vector< ReFileTriMeshPtr >& trimeshes, red::DynArray< CollShapeImportData >& collisionItems )
{
	String warnings = String::EMPTY();
	for( auto trimesh : trimeshes )
	{
		String collisionName( trimesh->getCollisionName().getData() );

		THandle< physics::ColliderMesh> shape = ReTriMeshToColliderMesh( trimesh, warnings );

		if( shape )
		{
			CollShapeImportData data;
			data.m_name = RED_NAME( collisionName );
			data.m_shape = shape;
			data.m_simulationType = ( physics::SimulationType )trimesh->getPhysicsMode();
			collisionItems.PushBack( data );
		}
	}
	return warnings;
}


void GenerateDestructionHierarchy( ReFileTransformPtr rootNode, CMesh* meshData, Uint32 numBones, red::DynArray< CName >& boneNames )
{
	// First, generated destruction hierarchy, starting from root bone node
	auto destructionHierarchy = meshData->CreateParameter< mesh::MeshParamDestructionStepData >();
	destructionHierarchy->m_offsets.Clear();

	red::Queue< ReFileTransformPtr > nodesToVisit = {red::PoolBackend()};
	red::BitSet64<256> isInstantRemovable; // c_maxDestructionActors = 256
	nodesToVisit.Reserve( numBones );
	boneNames.Reserve( numBones );
	destructionHierarchy->m_offsets.Reserve( numBones );
	isInstantRemovable.ClearAll();

	boneNames.PushBack( RED_NAME( rootNode->getName().getData() ) );
	nodesToVisit.Push( rootNode );
	Uint32 index = 0;
	while( !nodesToVisit.Empty() )
	{
		ReFileTransformPtr boneNode = nodesToVisit.Pop();
		physics::DestructionHierarchyOffset offset;
		const auto& children = boneNode->getChildren();
		if( children.size() )
		{
			offset.m_begin = boneNames.Size();
			offset.m_end = offset.m_begin - 1;
			for( auto child : children )
			{
				nodesToVisit.Push( child );
				boneNames.PushBack( RED_NAME( child->getName().getData() ) );
				++offset.m_end;
			}
		}
		if( boneNode->nodeFlags().getFlag("instantDestruction")  )
		{
			isInstantRemovable.Set( index );
		}
		++index;
		destructionHierarchy->m_offsets.PushBack( offset.m_combined );
	}
	destructionHierarchy->m_isInstantRemovable = isInstantRemovable.AsString();
	// Sort the bodies
	auto param = meshData->GetParameter< mesh::MeshParamPhysics >();

	// In case there's no physics data on the mesh (but the skeleton is there) quit here
	if( param == nullptr )
		return;

	THandle< physics::SystemResource > physics = param->GetPhysicsData();

	auto& objects = physics->GetBodies();

	std::sort( objects.Begin(), objects.End(), [boneNames]( const THandle< physics::SystemBody >& aBody, const THandle< physics::SystemBody >& bBody ) {
		if( !aBody || !bBody )
			return false;
		else
		{
			for( CName boneName : boneNames )
			{
				if( boneName == aBody->m_mappedBoneName )
					return true;
				else if( boneName == bBody->m_mappedBoneName )
					return false;
			}
		}
		return false;
	} );
}


void ReadCollisionsFromMeshes( const std::vector< ReFileMeshPtr > meshes, ReCollisionsImportContext& collContext )
{
	for( const auto m : meshes )
	{
		for( const auto ptr : m->getCollisions() )
		{
			switch( ptr->Type() )
			{
			case RE_TRIMESH_TYPE:
				collContext.triMeshes.push_back( std::static_pointer_cast< ReFileTriMesh >( ptr ) );
				break;
			case RE_COLLISIONCNV_TYPE:
				collContext.convexCollisions.push_back( std::static_pointer_cast< ReFileCollisionConvex >( ptr ) );
				break;
			case RE_COLLISIONBOX_TYPE:
				collContext.boxes.push_back( std::static_pointer_cast< ReFileCollisionBox >( ptr ) );
				break;
			case RE_COLLISIONCPS_TYPE:
				collContext.capsules.push_back( std::static_pointer_cast< ReFileCollisionCapsule >( ptr ) );
				break;
			case RE_COLLISIONSPH_TYPE:
				collContext.spheres.push_back( std::static_pointer_cast< ReFileCollisionSphere >( ptr ) );
				break;
			default:
				break;
			}
		}
	}
}


Int32 FindMappedBoneIndex( const String& collisionName, mesh::DataBuilder::TBones& bones )
{
	Uint32 offset = 0;
	if( collisionName.EndsWith( STR_COLL ) )
	{
		offset = STR_COLL.Length();
	}
	CName name = RED_NAME( red::StringView( collisionName.AsChar(), collisionName.Length() - offset ) );
	for( Uint32 i = 0; i < bones.Size(); i++ )
	{
		auto bone = bones[ i ];
		if( bone.m_name == name )
			return (Int32)i;
	}
	return -1;
}


Int32 FindMappedBoneIndex( const String& collisionName, red::DynArray< CName >& bones )
{
	Uint32 offset = 0;
	if( collisionName.EndsWith( STR_COLL ) )
	{
		offset = STR_COLL.Length();
	}
	CName name = RED_NAME( red::StringView( collisionName.AsChar(), collisionName.Length() - offset ) );
	for( Uint32 i = 0; i < bones.Size(); i++ )
	{
		auto bone = bones[ i ];
		if( bone == name )
			return (Int32)i;
	}
	return -1;
}


enum ColliderBodyType
{
	CBT_Convex,
	CBT_Trimesh,
	CBT_Sphere,
	CBT_Box,
	CBT_Capsule,
	CBT_Unknown
};


ColliderBodyType GetColliderBodyType(const THandle< physics::ICollider >& collider)
{
	if( collider->IsA< physics::ColliderConvex >() )
	{
		return ColliderBodyType::CBT_Convex;
	}
	else if( collider->IsA< physics::ColliderMesh >() )
	{
		return ColliderBodyType::CBT_Trimesh;
	}
	else if( collider->IsA< physics::ColliderSphere >() )
	{
		return ColliderBodyType::CBT_Sphere;
	}
	else if( collider->IsA< physics::ColliderBox >() )
	{
		return ColliderBodyType::CBT_Box;
	}
	else if( collider->IsA< physics::ColliderCapsule >() )
	{
		return ColliderBodyType::CBT_Capsule;
	}

	return ColliderBodyType::CBT_Unknown;
}


struct ColliderBodyPropertiesCache
{
	Float m_volumeModifier = 1.0f;

	ColliderBodyType m_bodyType = ColliderBodyType::CBT_Unknown;
};


void GetBodyPropertiesCache( THandle< physics::SystemResource >& physics, red::Queue< ColliderBodyPropertiesCache >& propertiesCache )
{
	const physics::Bodies& bodies = physics->GetBodies();
	for( const THandle< physics::SystemBody >& body : bodies )
	{
		auto& colliders = body->m_collisionShapes;

		for( const auto& collider : colliders )
		{
			if( collider->m_isImported )
			{
				ColliderBodyPropertiesCache cache;
				cache.m_bodyType = GetColliderBodyType( collider );

				cache.m_volumeModifier = collider->m_volumeModifier;
				propertiesCache.Push( cache );
			}
		}
	}
}


ImportResult ImportCollisionData( CMesh* meshData, const THandle< CREXMeshResourceImportOptions >& options, const ReCollisionsImportContext& collContext, mesh::DataBuilder& builder)
{
	String warningMessage = String::EMPTY();

	auto param = meshData->CreateParameter< mesh::MeshParamPhysics >();

	THandle< physics::SystemResource > physics = param->GetPhysicsData();
	
	Bool cleanPhysicsImport = options->m_cleanPhysicsImport;
	if( physics == nullptr || cleanPhysicsImport )
		physics = CreateHandle< physics::SystemResource >();
	
	// Used to store weight modifier values if types of colliders in re-imported mesh are matching.
	red::Queue< ColliderBodyPropertiesCache > propertiesCache;
	if( !cleanPhysicsImport )
	{
		GetBodyPropertiesCache( physics, propertiesCache );
		physics::Bodies& bodies = physics->GetBodies();
		for( const THandle< physics::SystemBody >& body : bodies )
		{
			auto& colliders = body->m_collisionShapes;
			// Remove colliders marked as imported
			colliders.Remove( std::remove_if( colliders.Begin(), colliders.End(), []( const THandle< physics::ICollider >& collider ) {
				return collider->m_isImported || collider->IsA< physics::ColliderMesh >();
			} ), colliders.End() );
		}

		// Remove bodies with no shapes
		bodies.Remove( std::remove_if( bodies.Begin(), bodies.End(), [ ]( const THandle< physics::SystemBody >& body ) {
			return body->m_collisionShapes.Empty();
		} ), bodies.End() );
	}
	red::DynArray< CollShapeImportData > collisionItems;

	// import file content
	warningMessage += ImportCollisionConvexes( collContext.convexCollisions, collisionItems );
	warningMessage += ImportCollisionBoxes( collContext.boxes, collisionItems );
	warningMessage += ImportCollisionSpheres( collContext.spheres, collisionItems );
	warningMessage += ImportCollisionCapsules( collContext.capsules, collisionItems );
	warningMessage += ImportCollisionTriMeshes( collContext.triMeshes, collisionItems );

	if( collisionItems.Size() > 0 )
	{
		// --- Pack all bodies not attached to joints into one SystemBody
		physics::SystemBodyBuildData staticSystemBodyInfo;
		staticSystemBodyInfo.m_name = RED_NAME_CONSTEXPR( "Imported collisions" );

		THandle< physics::SystemBody > notskinnedBody = nullptr;
		if( !options->m_cleanPhysicsImport )
		{
			const CName unskinnedFakeBone = RED_NAME_CONSTEXPR( "rootNotSkinned" ); // hardcoded to avoid useless includes
			const physics::Bodies& bodies = physics->GetBodies();
			auto it = std::find_if( bodies.Begin(), bodies.End(),
									[ unskinnedFakeBone ]( const THandle< physics::SystemBody >& body ) {
										return body->m_mappedBoneName == CName::NONE()
											|| body->m_mappedBoneName == unskinnedFakeBone;
									} );
			notskinnedBody = it != bodies.End() ? *it : nullptr;
		}
		Uint32 baseCacheLength = propertiesCache.Size();
		if( builder.m_bones.Size() == 0 )
		{
			for( auto item : collisionItems )
			{
				if( propertiesCache.Size() > 0 && baseCacheLength == collisionItems.Size() )
				{
					const ColliderBodyPropertiesCache c = propertiesCache.Pop();
					if( GetColliderBodyType( item.m_shape ) == c.m_bodyType )
					{
						item.m_shape->m_volumeModifier = c.m_volumeModifier;
					}
					else
					{
						warningMessage += String::Printf( "Collision item %hs type has changed, will not be keep its volume modifier!\n", item.m_name.AsChar() );
						RED_LOG_WARNING( "The collider type has changed, won't keep the volume modifier." );
					}
				}

				if( notskinnedBody )
				{
					notskinnedBody->m_collisionShapes.PushBack( item.m_shape );
				}
				else
				{
					staticSystemBodyInfo.m_collisionShapes.PushBack( item.m_shape );
				}
			}

			if( staticSystemBodyInfo.m_collisionShapes.Size() > 0 )
			{
				staticSystemBodyInfo.m_params.m_simulationType = physics::SimulationType::Static;
				auto body = physics::SystemBody::Create( staticSystemBodyInfo );
				if( body )
				{
					physics->AttachObject( body );
				}
				else
				{
					warningMessage += String::Printf( "Failed to create static body, collision will not be imported!\n" );
				}
			}
		}
		else
		{
			// --- create separate system body for each bone.
			for( auto item : collisionItems )
			{
				Int32 mappedIndex = FindMappedBoneIndex( item.m_name.AsChar(), builder.m_bones );

				if( mappedIndex == -1 )
				{
					warningMessage += String::Printf( "Collision item %hs doesn't have any bone mapped, will not be imported!\n", item.m_name.AsChar() );
					continue;
				}
				CName boneName = builder.m_bones[ mappedIndex ].m_name;

				THandle< physics::SystemBody > skinnedBody = nullptr;

				if( !options->m_cleanPhysicsImport )
				{
					const physics::Bodies& bodies = physics->GetBodies();
					auto it = std::find_if( bodies.Begin(), bodies.End(),
						[ boneName ]( const THandle< physics::SystemBody >& body )
					{
						return body->m_mappedBoneName == boneName;
					} );
					skinnedBody = it != bodies.End() ? *it : nullptr;
				}

				Transform xform = item.m_shape->m_localToBody;
				Transform rigTransform = Transform( builder.m_bones[ mappedIndex ].m_rig.GetTranslation(), builder.m_bones[ mappedIndex ].m_rig.ToQuat() );
				Transform shapeTransform = xform * rigTransform;
				item.m_shape->m_localToBody = shapeTransform;

				if( skinnedBody )
				{
					skinnedBody->m_collisionShapes.PushBack( item.m_shape );
				}
				else
				{
					physics::SystemBodyBuildData bodyInfo;
					bodyInfo.m_name = item.m_name;
					bodyInfo.m_mappedBoneName = boneName;
					bodyInfo.m_mappedBoneToBody = Transform::IDENTITY();
					bodyInfo.m_collisionShapes.PushBack( item.m_shape );
					bodyInfo.m_localToModel = rigTransform.GetInverse();
					bodyInfo.m_params.m_simulationType = item.m_simulationType;

					auto body = physics::SystemBody::Create( bodyInfo );
					if( body )
					{
						physics->AttachObject( body );
					}
					else
					{
						warningMessage += String::Printf( "Failed to create skinned body for %hs, some collisions will not be imported!\n", item.m_name.AsChar() );
					}
				}
			}
		}
	}

	// --- mkrupa 04.11.2019 --- This is a new physics definition import method, where we try to mimic the engine setup inside Re.
	warningMessage += ImportPhysicsDefinition( collContext.reMesh->getPhysics(), physics, options );

	// set imported physics
	if( physics->GetNumObjects() > 0 )
	{
		tools::RecomputeCOM( *physics );
		param->SetPhysicsData( physics );
	}
	else
	{
		meshData->RemoveParameter( param );
	}

	MESH_IMPORT_LOG( warningMessage.AsChar() );
	RED_LOG_WARNING( warningMessage.AsChar() );
	return ImportResult::Success( nullptr, warningMessage );
}


String ImportPhysicsDefinition(const ReFilePhysics& rePhysics, THandle< physics::SystemResource > physicsRes, const THandle<CREXMeshResourceImportOptions>& options)
{
	red::Map< Int32, THandle< physics::SystemBody > > bodiesMap;
	String warnings = String::EMPTY();
	for (const auto& actor : rePhysics.getActors())
	{
		physics::SystemBodyBuildData bodyInfo;
		bodyInfo.m_name = RED_NAME(actor->getName().getData());
		bodyInfo.m_mappedBoneName = RED_NAME(actor->getMappedBoneName());
		bodyInfo.m_mappedBoneToBody = Transform::IDENTITY();

		for (auto& s : actor->getCollisionShapes())
		{
			switch( s->Type() )
			{
			case RE_TRIMESH_TYPE:
				bodyInfo.m_collisionShapes.PushBack( ReTriMeshToColliderMesh( std::static_pointer_cast< ReFileTriMesh >( s ), warnings ) );
				break;
			case RE_COLLISIONCNV_TYPE:
				bodyInfo.m_collisionShapes.PushBack( ReConvexToColliderConvexShape( std::static_pointer_cast< ReFileCollisionConvex >( s ), warnings ) );
				break;
			case RE_COLLISIONBOX_TYPE:
				bodyInfo.m_collisionShapes.PushBack( ReBoxToColliderBoxShape( std::static_pointer_cast< ReFileCollisionBox >( s ) ) );
				break;
			case RE_COLLISIONCPS_TYPE:
				bodyInfo.m_collisionShapes.PushBack( ReCapsuleToColliderCapsule( std::static_pointer_cast< ReFileCollisionCapsule >( s ) ) );
				break;
			case RE_COLLISIONSPH_TYPE:
				bodyInfo.m_collisionShapes.PushBack( ReSphereToColliderSphere( std::static_pointer_cast< ReFileCollisionSphere >( s ) ) );
				break;
			default:
				warnings += String::Printf( "Unrecognized collision type for shape %hs of actor %hs\n", s->getCollisionName().getData(), actor->getName().getData() );
				break;
			}
		}

		Re::Matrix4 reLocalToBody;
		actor->getTransform( reLocalToBody );
		reLocalToBody.ApplySpaceScale( FROM_RE_SCALE );
		Matrix localToBody( reLocalToBody[ 0 ] );
		bodyInfo.m_localToModel = localToBody.ToXform();

		bodyInfo.m_params.m_simulationType = static_cast<physics::SimulationType>(actor->getActorType());

		auto body = physics::SystemBody::Create( bodyInfo );
		if (body)
		{
			bodiesMap[ actor->getId() ] = body;
			physicsRes->AttachObject( body );
		}
		else
		{
			warnings += String::Printf( "Failed to create SystemBody for actor %hs\n", actor->getName().getData() );
		}
	}

	const std::vector< std::shared_ptr< RePhysicalJoint > >& joints = rePhysics.getJoints();
	for( Uint32 i = 0; i < joints.size(); ++i )
	{
		const auto& jnt = joints[ i ];
		if (bodiesMap.FindPtr(jnt->getActorA()) == nullptr || bodiesMap.FindPtr(jnt->getActorB()) == nullptr)
		{
			RED_LOG_ERROR( "Invalid joints definition in Re file!" );
			warnings += String::Printf( "Invalid joint definition for joint %d\n", i );
			continue;
		}

		THandle<physics::SystemJoint> joint = CreateHandle<physics::SystemJoint>();

		joint->SetIsBreakable( jnt->isBreakable() );
		joint->SetBreakingForce( jnt->getBreakingForce() );
		joint->SetBreakingTorque( jnt->getBreakingTorque() );

		Re::Matrix4 reLocalToBody;
		
		Matrix jointMatrix( jnt->getTransform()[0] );

		THandle< physics::SystemBody > actorA = bodiesMap.Find( jnt->getActorA() ).Value();
		THandle< physics::SystemBody > actorB = bodiesMap.Find( jnt->getActorB() ).Value();

		joint->SetupWorld( jointMatrix.GetTranslation() * FROM_RE_SCALE, jointMatrix.ToQuat(), actorA, actorB, actorA->m_localToModel.ToInvMatrix(), actorB->m_localToModel.ToInvMatrix() );

		physicsRes->AttachObject( joint );
	}
	return warnings;
}


void ImportDestructionHierarchy(CMesh* meshData, ReFileSkeletonPtr skeleton, mesh::DataBuilder& builder)
{
	ReFileBonePtr skeletonRoot = skeleton->getSkeletonRoot();
	ReFileTransformPtr destructionHierarchyRoot = skeletonRoot->getChildren()[ 0 ];
	red::DynArray< CName > boneNames = {red::PoolBackend()};
	GenerateDestructionHierarchy( destructionHierarchyRoot, meshData, builder.m_bones.Size(), boneNames );
	const std::vector< ReBonesDynamicLink >& bonds = skeleton->getDynamicLinks();
	if( bonds.size() > 0 )
	{
		mesh::DestructionBonds bondsFound;
		auto destructionBonds = meshData->CreateParameter< mesh::MeshParamDestructionBonds >();

		for( const ReBonesDynamicLink& bond : bonds )
		{
			Int32 indexA = FindMappedBoneIndex( bond.boneA.getData(), boneNames );
			Int32 indexB = FindMappedBoneIndex( bond.boneB.getData(), boneNames );
			RED_ASSERT( indexA >= 0, "Bone name (%hs) from bond not found in mesh bones!", bond.boneA.getData() );
			RED_ASSERT( indexB >= 0, "Bone name (%hs) from bond not found in mesh bones!", bond.boneB.getData() );
			RED_ASSERT( bond.weight >= std::numeric_limits< physics::BondHealth >::min() && bond.weight <= std::numeric_limits< physics::BondHealth >::max(), "Bond with weight outside of range!", bond.boneB.getData() );
			physics::BondIndexHelper index( (physics::DestructionActorIndex)indexA, (physics::DestructionActorIndex)indexB );
			bondsFound.PushBack( {index.m_bondIndex, (physics::BondHealth)bond.weight} );
		}
		destructionBonds->SetDestructionBonds( bondsFound );
	}

	auto paramBCM = meshData->CreateParameter< mesh::MeshParamDestructionBoneChunkMapping >();
	paramBCM->OnMeshChanged( *meshData );

	auto paramDCIO = meshData->CreateParameter< mesh::MeshParamDestructionChunkIndicesOffsets >();
	paramDCIO->OnMeshChanged( *meshData );
}

ImportResult ClearDestructionData( CMesh* meshData )
{
	String warningMessage = String::EMPTY();

	Bool dataRemoved = false;
	if( auto param = meshData->GetParameter< mesh::MeshParamDestructionBonds >() )
	{
		meshData->RemoveParameter( param );
		dataRemoved = true;
	}

	if( auto param = meshData->GetParameter< mesh::MeshParamDestructionBoneChunkMapping >() )
	{
		meshData->RemoveParameter( param );
		dataRemoved = true;
	}

	if( auto param = meshData->GetParameter< mesh::MeshParamDestructionChunkIndicesOffsets >() )
	{
		meshData->RemoveParameter( param );
		dataRemoved = true;
	}

	if( auto param = meshData->GetParameter< mesh::MeshParamDestructionStepData >() )
	{
		meshData->RemoveParameter( param );
		dataRemoved = true;
	}
	
	if( dataRemoved )
	{
		warningMessage += "Removing destruction data from mesh that used to have it. Have you forgotten to tick the Import Destruction Hierarchy box?\n";
	}
	MESH_IMPORT_LOG( warningMessage.AsChar() );
	RED_LOG_WARNING( warningMessage.AsChar() );
	return ImportResult::Success( nullptr, warningMessage );
}
