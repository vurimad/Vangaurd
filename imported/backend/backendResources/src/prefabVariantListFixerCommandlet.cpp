/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resaveCommandlet.h"

#include "../../../common/world/include/worldPrefab.h"
#include "../../../common/world/include/worldNode.h"
#include "../../../common/world/include/worldPrefabNode.h"
#include "../../../common/world/include/worldPrefabVariantsList.h"
#include "../../../common/redReflection/include/editableView.h"

class PrefabVariantListFixerCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( PrefabVariantListFixerCommandlet );

public:
	PrefabVariantListFixerCommandlet();

private:
	virtual bool OnResourcePreSave( CResource & resource ) override final;
};

RTTI_BEGIN_TYPE( PrefabVariantListFixerCommandlet )
RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

PrefabVariantListFixerCommandlet::PrefabVariantListFixerCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "fixVariantLists" ) )
{
}

bool PrefabVariantListFixerCommandlet::OnResourcePreSave( CResource& resource )
{
	if ( !resource.IsA< world::Prefab >() )
	{
		RED_LOG_ERROR( "Resource %hs cannot be processed by lockPrefabs commandlet", resource.GetPath().ToDebugString() );
		return false;
	}

	world::Prefab& prefab = static_cast<world::Prefab&>( resource );
	red::HashSet< SerializableID > variantListsIds;

	Bool resave = false;
	for ( world::NodePtr node : prefab.GetNodes() )
	{
		if ( THandle< world::PrefabNode > prefabNode = Cast< world::PrefabNode >( node ) )
		{
			world::PrefabVariantsList& variantsList = prefabNode->GetVariantsList();
			if ( variantListsIds.Exist( variantsList.GetID() ) )
			{
				THandle< world::PrefabVariantsList > newVariantList = CreateHandle< world::PrefabVariantsList >();
				newVariantList->SetActiveVariants( variantsList.GetActiveVariants() );

				tools::EditableViewPtr view = prefabNode->CreateView();
				view->SetValue( IRTTIContext::GetDefault(), rtti::AccessPath()[ "enabledVariants" ], rtti::ValuePtr(), rtti::ValueHolder::CreateHandle( ToStringDirect( newVariantList->GetID() ).AsChar() ) );

				resave = true;
			}
			else
			{
				variantListsIds.Insert( variantsList.GetID() );
			}
		}
	}

	return resave;
}