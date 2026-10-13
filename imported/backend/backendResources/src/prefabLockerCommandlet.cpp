/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resaveCommandlet.h"
#include "../../../common/world/include/worldPrefab.h"

class PrefabLockerCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( PrefabLockerCommandlet );

public:
	PrefabLockerCommandlet();
	virtual const char* GetOneLiner() const override final;

private:
	virtual bool OnResourcePreSave( CResource& resource ) override final;
};

RTTI_BEGIN_TYPE( PrefabLockerCommandlet )
	RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

//////////////////////////////////////////////////////////////////////////

PrefabLockerCommandlet::PrefabLockerCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "lockPrefabs" ) )
{}

const char* PrefabLockerCommandlet::GetOneLiner() const
{
	return "Update 'isLocked' property of prefab resources";
}

bool PrefabLockerCommandlet::OnResourcePreSave( CResource & resource )
{
	if ( resource.IsA< world::Prefab >() )
	{
		world::Prefab& prefab = static_cast<world::Prefab&>( resource );
		prefab.SetLockState( true );
		return prefab.IsLocked(); // don't save prefab if locking failed
	}

	RED_LOG_ERROR( "Resource %hs cannot be processed by lockPrefabs commandlet", resource.GetPath().ToDebugString() );
	return false;
}
