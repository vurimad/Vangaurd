/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resaveCommandlet.h"

#include "../../backendEngine/include/commandletRenderInstance.h"
#include "../../../common/gameWorkspots/include/workspotAnimSetCollector.h"

class WorkspotResaveCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( WorkspotResaveCommandlet );
public:
	WorkspotResaveCommandlet();
	virtual ~WorkspotResaveCommandlet() = default;

	virtual const AnsiChar* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;
	virtual Bool InitializeEnvironment() override final;
	virtual void TeardownEnvironment() override final;

private:
	virtual Bool OnResourcePreSave( CResource& resource ) override final;
	Bool ProcessWorkspot( work::WorkspotResource& workspot );

	commandlet::RenderInstance m_renderInstance;
	red::DynArray< res::ResourcePath > m_entityPaths{ red::PoolBackend() };
	red::DynArray< res::ResourcePath > m_parentPaths{ red::PoolBackend() };
	red::UniquePtr< tools::WorkspotAnimSetCollectorCache, red::PoolBackend > m_animSetCollectorCache;
};

RTTI_BEGIN_TYPE( WorkspotResaveCommandlet )
	RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

WorkspotResaveCommandlet::WorkspotResaveCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "workspotResave" ) )
{
}

const AnsiChar* WorkspotResaveCommandlet::GetOneLiner() const
{
	return nullptr;
}

void WorkspotResaveCommandlet::PrintHelp() const 
{
}

Bool WorkspotResaveCommandlet::InitializeEnvironment()
{
	const Bool result = ResaveCommandlet::InitializeEnvironment() && m_renderInstance.Initialise();

	m_entityPaths.Clear();

	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_base\\man_base.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_fat\\man_fat.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\player\\player_ma_fpp.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\player\\player_wa_fpp.ent" ) );

	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\blackwater.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\bodyguard_dex.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\capitan_reyes.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\celebrity_chrome.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\clair.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\dex.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\evelyn.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\fingers.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\hanako.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\haru_kasai.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\jackie_welles.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\judy.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\kerry_eurodyne.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\maman_brigitte.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\militech_agent.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\misty.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\panam.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\placide.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\psycho.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\ripper_doc.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\river_ward.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\rogue.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\royce.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\royce_dead.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\saburo_arasaka.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\scavenger_boss.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\silverhand.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\sobchuk.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\t_bug.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\wilson.ent" ) );
	m_entityPaths.PushBack( res::ResourcePath::Build( "base\\characters\\entities\\main_npc\\yorinobu_arasaka.ent" ) );

	m_parentPaths.Clear();

	m_parentPaths.PushBack( res::ResourcePath::EMPTY );
	m_parentPaths.PushBack( res::ResourcePath::EMPTY );
	m_parentPaths.PushBack( res::ResourcePath::EMPTY );
	m_parentPaths.PushBack( res::ResourcePath::EMPTY );
	m_parentPaths.PushBack( res::ResourcePath::EMPTY );
	m_parentPaths.PushBack( res::ResourcePath::EMPTY );

	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_base\\man_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_fat\\man_fat.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_base\\man_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_base\\man_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_base\\man_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_base\\man_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_base\\man_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_base\\man_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_base\\man_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\woman_base\\woman_base.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_big\\man_big.ent" ) );
	m_parentPaths.PushBack( res::ResourcePath::Build( "base\\characters\\base_entities\\man_base\\man_base.ent" ) );

	m_animSetCollectorCache = red::CreateUniquePtr< tools::WorkspotAnimSetCollectorCache, red::PoolBackend >( m_entityPaths );

	return result;
}

void WorkspotResaveCommandlet::TeardownEnvironment()
{
	m_animSetCollectorCache = nullptr; // Destroy cached entities before systems tear down

	m_renderInstance.Shutdown();
	ResaveCommandlet::TeardownEnvironment();
}

Bool WorkspotResaveCommandlet::OnResourcePreSave( CResource& resource )
{
	if ( resource.IsA< work::WorkspotResource >() )
	{
		return ProcessWorkspot( static_cast< work::WorkspotResource& >( resource ) );
	}
	
	RED_LOG_ERROR( "Resource '%hs' cannot be processed by workspotResave commandlet.", resource.GetPath().ToDebugString() );
	return false;
}

Bool WorkspotResaveCommandlet::ProcessWorkspot( work::WorkspotResource& workspotResource )
{
	if ( workspotResource.m_workspotTree->EDITOR_GetHasEntityPathsGenerated() == false )
	{
		red::DynArray< res::ResourcePath > acceptedEntities{ red::PoolBackend() };
		red::StringBuilder<String> builder;
		std::function< void( const String&, tools::GenerateAnimsetsMessageType ) > writerFunc = [ &builder ]( const String& string, tools::GenerateAnimsetsMessageType msgType )
		{
			builder.Append( string );
		};
		tools::GenerateFinalAnimSets( *m_animSetCollectorCache, m_entityPaths, HandleFromPtr( &workspotResource ), workspotResource.m_workspotTree, &acceptedEntities, writerFunc, workspotResource.GetPath().ToDebugString() );
		if ( acceptedEntities.Empty() )
		{
			const red::DynArray< String > strings = builder.ToString().Split( "\n" ); // One big string cannot be printed with RED_LOG_ERROR (message is truncated).
			for ( const String& string : strings )
			{
				RED_LOG_ERROR( string.AsChar() );
			}
		}

		red::DynArray< CName >& entityNamesInWorkspot = workspotResource.m_workspotTree->EDITOR_AccessEntityPaths();
		red::DynArray< res::ResourcePath > entityPathsInWorkspot{ red::PoolBackend() };
		for ( const res::ResourcePath& acceptedEntity : acceptedEntities )
		{
			const Uint32 entityIndex = m_entityPaths.GetIndex( acceptedEntity );
			const res::ResourcePath& parentPath = m_parentPaths[ entityIndex ];
			if ( !acceptedEntities.Exist( parentPath ) )
			{
				red::alg::PushBackUnique( entityNamesInWorkspot, RED_NAME( acceptedEntity.ToStringView().Data() ) );
			}
		}
		for ( const CName& entityName : entityNamesInWorkspot )
		{
			entityPathsInWorkspot.PushBack( res::ResourcePath::Build( entityName.AsChar() ) );
		}

		workspotResource.m_workspotTree->NotifyPropertyChanged( rtti::AccessPath( "entitiesPaths" ) );
		workspotResource.m_workspotTree->EDITOR_SetHasEntityPathsGenerated( true );

		tools::WorkspotAnimSetCollectorCache localAnimSetCollectorCache( entityPathsInWorkspot );
		GenerateFinalAnimSets( localAnimSetCollectorCache, entityPathsInWorkspot, HandleFromPtr( &workspotResource ), workspotResource.m_workspotTree, nullptr, nullptr, workspotResource.GetPath().ToDebugString() );
		workspotResource.m_workspotTree->EDITOR_ProcessAnimSetChanges();
	}
	return true;
}
