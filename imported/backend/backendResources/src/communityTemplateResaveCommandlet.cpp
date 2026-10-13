/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resaveCommandlet.h"
#include "../../../common/redReflection/include/util.h"

#include "../../../common/gamePopulation/include/communitySpawnEntry.h"
#include "../../../common/gamePopulation/include/communitySpawnPhase.h"
#include "../../../common/gamePopulation/include/communityTemplate.h"
#include "../../../common/gamePopulation/include/communityArea.h"

class CommunityTemplateResaveCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( CommunityTemplateResaveCommandlet );

public:
	CommunityTemplateResaveCommandlet();
	virtual ~CommunityTemplateResaveCommandlet();

	virtual const char* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;

private:
	virtual bool OnResourcePreSave( CResource & resource ) override final;
};

RTTI_BEGIN_TYPE( CommunityTemplateResaveCommandlet )
	RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

CommunityTemplateResaveCommandlet::CommunityTemplateResaveCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "communityTemplateResave" ) )
{}

CommunityTemplateResaveCommandlet::~CommunityTemplateResaveCommandlet()
{}

const char* CommunityTemplateResaveCommandlet::GetOneLiner() const
{
	return "Update 'alwaysSpawned' property of community resources";
}

void CommunityTemplateResaveCommandlet::PrintHelp() const 
{
	
}

bool CommunityTemplateResaveCommandlet::OnResourcePreSave( CResource & resource )
{
	if ( resource.IsA< community::CommunityTemplate >() )
	{
		community::CommunityTemplate& communityTemplate = static_cast< community::CommunityTemplate& >( resource );
		const THandle< community::CommunityTemplateData >& templateData = communityTemplate.GetCommunityTemplateData();
		const community::CommunityTemplateData::EntriesList& communityEntries = templateData->GetEntries();

		for ( auto entry : communityEntries )
		{
			if ( entry )
			{
				for ( auto phase : entry->GetSpawnPhases() )
				{
					if ( phase )
					{
						if ( phase->IsAlwaysSpawned() )
						{
							phase->SetAlwaysSpawned( game::AlwaysSpawnedState::Default );
						}
					}
				}
			}
		}
		return true;
	}
	
	RED_LOG_ERROR( "Resource %hs cannot be processed by CommunityTemplateResave commandlet", resource.GetPath().ToDebugString() );
	return false;
}

