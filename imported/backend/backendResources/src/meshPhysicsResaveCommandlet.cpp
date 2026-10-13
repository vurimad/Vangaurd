/**
 * Copyright c 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resaveCommandlet.h"
#include "../../backendMesh/include/meshDataView.h"
#include "../../backendMesh/include/meshPhysicsUtils.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshUtils.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"
#include "../../../common/redReflection/include/util.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemBody.h"
#include "../../../common/physics/include/physXNatives.h"
#include "../../../common/physics/include/physicsSystemUtils.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/redReflection/include/packageIterator.h"
#include "../../../common/game/include/hitRepresentationComponent.h"

class MeshPhysicsResaveCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE(MeshPhysicsResaveCommandlet);

public:

	MeshPhysicsResaveCommandlet();
	virtual ~MeshPhysicsResaveCommandlet();

	virtual const char* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;

private:
	virtual bool OnResourcePreSave( CResource & resource ) override final;

	bool ProcessMesh( CMesh & mesh );

	red::Map< CName, CName > m_materialRemapTable = {red::PoolBackend()};
};

RTTI_BEGIN_TYPE( MeshPhysicsResaveCommandlet )
RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()


MeshPhysicsResaveCommandlet::MeshPhysicsResaveCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "meshPhysics" ) )
{
	m_materialRemapTable.Insert( RED_NAME_CONSTEXPR("tarmac.physmat"), RED_NAME_CONSTEXPR("asphalt.physmat") );
	m_materialRemapTable.Insert( RED_NAME_CONSTEXPR( "character_cyberware.physmat" ), RED_NAME_CONSTEXPR( "cyberware_flesh.physmat" ) );
}

MeshPhysicsResaveCommandlet::~MeshPhysicsResaveCommandlet()
{}

const char* MeshPhysicsResaveCommandlet::GetOneLiner() const
{
	return nullptr;	
}

void MeshPhysicsResaveCommandlet::PrintHelp() const
{
	
}

bool MeshPhysicsResaveCommandlet::OnResourcePreSave( CResource & resource )
{
	auto path = resource.GetPath();
	if( resource.IsA< CMesh >() && !path.ToString().Contains( "_generated" ) && !path.ToString().Contains( "_external" ) )
	{		
		return ProcessMesh( static_cast< CMesh& >( resource ) );		
	}

	return false;
}

bool MeshPhysicsResaveCommandlet::ProcessMesh( CMesh & meshData )
{
	Bool hasChanged = false;

	auto path = meshData.GetPath();
	auto physSystem = mesh::utils::GetActivePhysics( meshData );
	if( physSystem )
	{
		Bool needsResave = false;
		for( auto& body : physSystem->GetBodies() )
		{
			for( auto& collider : body->m_collisionShapes )
			{
				if( collider->IsA<physics::ColliderMesh>() )
				{
					return false;
				}
				if( m_materialRemapTable.KeyExist( collider->m_material ) )
				{
					collider->m_material = m_materialRemapTable[ collider->m_material ];
				}
			}
			if( body->m_params.m_mass <= 0.f )
			{
				needsResave = true;
			}
		}
		return needsResave ? tools::RecomputeCOM( *physSystem ) : needsResave;
	}
	return false;
}


class HitRepComponentResaveCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( HitRepComponentResaveCommandlet );

public:

	HitRepComponentResaveCommandlet();
	virtual ~HitRepComponentResaveCommandlet();

	virtual const char* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;

private:
	virtual bool OnResourcePreSave( CResource & resource ) override final;

	bool ProcessEntity( ent::EntityTemplate & ent );
};


RTTI_BEGIN_TYPE( HitRepComponentResaveCommandlet )
RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

HitRepComponentResaveCommandlet::HitRepComponentResaveCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "entityHitRep" ) )

{
}

HitRepComponentResaveCommandlet::~HitRepComponentResaveCommandlet()
{}

const char* HitRepComponentResaveCommandlet::GetOneLiner() const
{
	return nullptr;
}

void HitRepComponentResaveCommandlet::PrintHelp() const
{

}

bool HitRepComponentResaveCommandlet::OnResourcePreSave( CResource & resource )
{
	auto path = resource.GetPath();
	if( resource.IsA< ent::EntityTemplate >() && !path.ToString().Contains( "_generated" ) )
	{
		return ProcessEntity( static_cast<ent::EntityTemplate&>(resource) );
	}

	return false;
}

bool HitRepComponentResaveCommandlet::ProcessEntity( ent::EntityTemplate & entity )
{
	auto layout = entity.Editor_GetLocalDataLayout();
	red::PackageObjectIterator iterator( layout.package );
	iterator.SetCurrentIndex( layout.entityIndex );
	const rtti::ClassType * entityClassType = nullptr;
	entityClassType = iterator.GetType();
	iterator.Next();
	game::HitRepresentationComponent obj;
	const rtti::ClassType* target = rtti::ExtractClass( &obj );
	while( iterator.IsValid() )
	{
		entityClassType = iterator.GetType();
		if( entityClassType == target )
			return true;
		iterator.Next();
	}
	return false;
}
