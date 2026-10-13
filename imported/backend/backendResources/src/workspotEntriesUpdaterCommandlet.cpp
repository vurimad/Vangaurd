/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resaveCommandlet.h"

#include "../../../common/gameWorkspots/include/workspotResource.h"
#include "../../../common/gameWorkspots/include/workspotTreeItems.h"
#include "../../../common/worldEntities/include/entity.h"
#include "../../../common/world/include/runtimeInfo.h"
#include "../../../common/redReflection/include/handle.inl"
#include "../../../common/gameWorkspots/include/workspotAnimSetCollector.h"
#include "../../backendWorkspots/src/workspotTree.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../backendAnimation/src/animGraphBackend_Utils.h"
#include "../../backendEngine/include/commandletRenderInstance.h"


class WorkspotEntriesUpdaterCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( WorkspotEntriesUpdaterCommandlet );
public:
	WorkspotEntriesUpdaterCommandlet();
	virtual ~WorkspotEntriesUpdaterCommandlet() = default;

	virtual bool Execute( const red::CommandLine& options ) override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
private:
	virtual Bool OnResourcePreSave( CResource& resource ) override final;

	const Uint32 AddReactionSequences( work::WorkspotResource& workspot, const red::DynArray<THandle< anim::AnimSet >>& animSets );
	const Uint32 AddEntryAnimations( work::WorkspotResource& workspot, THandle<work::IContainerEntry> sequence, const red::DynArray<THandle< anim::AnimSet >>& animSets );
	const Uint32 AddExitAnimations( work::WorkspotResource& workspot, THandle<work::IContainerEntry> sequence, const red::DynArray<THandle< anim::AnimSet >>& animSets );
	const Uint32 AddFastExitAnimations( work::WorkspotResource& workspot, THandle<work::IContainerEntry> sequence, const red::DynArray<THandle< anim::AnimSet >>& animSets );
	const Uint32 AddBumpSequences( work::WorkspotResource& workspot, const red::DynArray<THandle< anim::AnimSet >>& animSets );
	void RemoveExisingGates( work::WorkspotResource& workspot );

	THandle<anim::AnimSet> GetAnimSet( TResAsyncRef< anim::AnimSet > animSetRef );
	Bool ProcessWorkspot( work::WorkspotResource& workspot );

	Bool m_safeMode = false;
	Bool m_generateReaction = false;
	Bool m_generateGates = false;
	Bool m_generateFastExits = false;
	Bool m_generateBumps = false;
	Bool m_purgeGates = false;


	red::UniquePtr<tools::WorkspotAnimSetCollectorCache, red::PoolBackend> m_animSetCollectorCache;

	red::HashSet<res::ResourcePath> m_failedAnimSet { red::PoolBackend() };
	red::DynArray<red::String> m_successResourceMessages { red::PoolBackend() };
	red::DynArray<red::String> m_failedResourceMessages { red::PoolBackend() };
	commandlet::RenderInstance m_renderInstance;
};

RTTI_BEGIN_TYPE( WorkspotEntriesUpdaterCommandlet )
RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

WorkspotEntriesUpdaterCommandlet::WorkspotEntriesUpdaterCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "workspotEntriesUpdater" ) )
{
}

bool WorkspotEntriesUpdaterCommandlet::Execute(const red::CommandLine& options)
{
	//if running on safe mode commandlet will only process base entities
	m_safeMode = options.HasOption("safeMode");
	m_generateReaction =  options.HasOption("generateReaction");
	m_generateGates = options.HasOption("generateGates");
	m_generateFastExits = options.HasOption( "generateFastExits" );
	m_purgeGates = options.HasOption( "purgeGates" );
	m_generateBumps = options.HasOption( "generateBumps" );

	if ( m_generateReaction || m_generateBumps )
	{
		game::data::InitTweakDB();
		game::data::GetTweakDB().LoadRawFiles();
		RED_LOG_WARNING("Reaction sequences will be generated");
	}
	else 
	{
		RED_LOG_WARNING("Missing option 'generateReaction', Reaction sequence will not be generated.");
	}
	if( m_purgeGates )
	{
		RED_LOG_WARNING( "Existing entries and exits will be removed" );
	}	
	if( m_generateFastExits )
	{
		RED_LOG_WARNING( "FastExits will be generated" );
	}

	if (m_generateGates)
	{
		RED_LOG_WARNING("Entries and exits will be generated");
	}
	else 
	{
		RED_LOG_WARNING("Missing option 'generateGates', Entries and exits will be not generated.");
	}

	if( m_generateBumps )
	{
		RED_LOG_WARNING( "Bump sequences will be generated" );
	}
	else
	{
		RED_LOG_WARNING( "Missing option 'generateBumps', Bumps will be not generated." );
	}

	return ResaveCommandlet::Execute(options);
}

Bool WorkspotEntriesUpdaterCommandlet::InitializeEnvironment()
{
	world::RuntimeInfo runtimeInfo( world::RuntimeMode::Preview );

	Bool res = ResaveCommandlet::InitializeEnvironment() && m_renderInstance.Initialise();

	m_animSetCollectorCache = red::CreateUniquePtr< tools::WorkspotAnimSetCollectorCache, red::PoolBackend >();

	return res;
}

void WorkspotEntriesUpdaterCommandlet::TeardownEnvironment()
{

	m_animSetCollectorCache->Clear();

	if (m_generateReaction)
	{
		game::data::DeinitTweakDB();
	}

	//log all skipped workspots
	if (m_successResourceMessages.Size() > 0) 
	{
		RED_LOG_INFO( "Following workspots are processed:");

		for (const auto& msg : m_successResourceMessages) 
		{
			RED_LOG_INFO("	%hs", msg.AsChar() );
		}
	}

	if (m_failedResourceMessages.Size() > 0) 
	{
		RED_LOG_ERROR( "Following workspots are not processed:");

		for (const auto& msg : m_failedResourceMessages) 
		{
			RED_LOG_ERROR("	%hs", msg.AsChar() );
		}
	}

	m_renderInstance.Shutdown();

	ResaveCommandlet::TeardownEnvironment();
}

const Uint32 WorkspotEntriesUpdaterCommandlet::AddReactionSequences(work::WorkspotResource& workspot, const red::DynArray<THandle< anim::AnimSet >>& animSets)
{
	if (!m_generateReaction) 
	{
		return 0;
	}
	return tools::helper::GenerateReactionsSequences(workspot.m_workspotTree.Get(), animSets);
}

const Uint32 WorkspotEntriesUpdaterCommandlet::AddBumpSequences( work::WorkspotResource& workspot, const red::DynArray<THandle< anim::AnimSet >>& animSets )
{
	if( !m_generateBumps )
	{
		return 0;
	}
	return tools::helper::GenerateBumpSequences( workspot.m_workspotTree.Get(), animSets );
}

const Uint32 WorkspotEntriesUpdaterCommandlet::AddEntryAnimations(work::WorkspotResource& workspot, THandle<work::IContainerEntry> sequence, const red::DynArray<THandle< anim::AnimSet >>& animSets)
{
	red::DynArray<THandle<work::EntryAnim>> outResult;
	tools::helper::GenerateEntryAnimations(sequence,workspot.m_workspotTree.Get(), animSets, outResult);
	return outResult.Size();
}

void WorkspotEntriesUpdaterCommandlet::RemoveExisingGates( work::WorkspotResource& workspot )
{
	const red::FixedSizeFunction< void( THandle<work::IEntry>& ) > preFun = []( THandle<work::IEntry>& entry )
	{

		if( const THandle<work::IContainerEntry> seq = Cast<work::IContainerEntry>( entry ) )
		{
			for( Int32 i = Int32( seq->m_list.Size() ) - 1; i >= 0; --i )
			{
				const work::IEntry* childEntry = seq->m_list[i].Get();
				CName animName;

				if( auto ptr = Cast<work::FastExit>( childEntry ) )
				{
					seq->m_list.RemoveAt( i );
					continue;
				}
				if( auto ptr = Cast<work::ExitAnim>( childEntry ) )
				{
					animName = ptr->m_animName;
				}
				else if( auto ptr = Cast<work::EntryAnim>( childEntry ) )
				{
					animName = ptr->m_animName;
				}

				if( !animName )
				{
					continue;
				}

				String strName = animName.AsChar();
				String left;
				String right;
				strName.Split( String( "__to__" ), &left, &right );

				//Bunch of magical conditions to purge resources from previous commandlets errors, can be removed at some point
				if( right.BeginsWith( left ) || strName.Contains( "fast_exit" ) || strName.Contains( "over_bench" ) || strName.Contains("turn0__01__turn0__01") )
				{
					seq->m_list.RemoveAt( i );
				}
			}
		}
	};

	const red::FixedSizeFunction< void( THandle<work::IEntry>& ) > postFun;
	const THandle<work::IContainerEntry> rootSequence = Cast<work::IContainerEntry>( workspot.m_workspotTree->m_rootEntry );
	rootSequence->ForEachNode( preFun, postFun );
}

const Uint32 WorkspotEntriesUpdaterCommandlet::AddExitAnimations(work::WorkspotResource& workspot, THandle<work::IContainerEntry> sequence, const red::DynArray<THandle< anim::AnimSet >>& animSets)
{
	red::DynArray<THandle<work::ExitAnim>> outResult;
	tools::helper::GenerateExitAnimations(sequence,workspot.m_workspotTree.Get(), animSets, outResult);
	return outResult.Size();
}

const Uint32 WorkspotEntriesUpdaterCommandlet::AddFastExitAnimations( work::WorkspotResource& workspot, THandle<work::IContainerEntry> sequence, const red::DynArray<THandle< anim::AnimSet >>& animSets )
{
	red::DynArray<THandle<work::FastExit>> outResult;
	tools::helper::GenerateFastExitAnimations( sequence, workspot.m_workspotTree.Get(), animSets, outResult );
	return outResult.Size();
}

THandle<anim::AnimSet> WorkspotEntriesUpdaterCommandlet::GetAnimSet(TResAsyncRef< anim::AnimSet > animSetRef)
{
	if ( !animSetRef )
	{
		return nullptr;
	}
	auto token = animSetRef.IssueLoadingRequest();
	while (!token->HasFinished())
	{

	}
	auto animSet = Cast<anim::AnimSet>( token->GetResource() );
	return animSet;
}

Bool WorkspotEntriesUpdaterCommandlet::OnResourcePreSave(CResource & resource)
{
	if ( resource.IsA< work::WorkspotResource >() )
	{
		return ProcessWorkspot( static_cast< work::WorkspotResource& >( resource ) );
	}

	m_failedResourceMessages.PushBack(red::String::Printf( "Resource '%hs' cannot be processed by '%s' commandlet.", resource.GetPath().ToDebugString(), GetName().AsChar() ));
	return false;
}

Bool WorkspotEntriesUpdaterCommandlet::ProcessWorkspot(work::WorkspotResource& workspot)
{
	//save and generate final animset
	const red::DynArray< CName >& entityPathsInWorkspot = workspot.m_workspotTree->EDITOR_AccessEntityPaths();
	red::DynArray< res::ResourcePath > resourcePaths{ red::PoolBackend() };
	resourcePaths.Reserve( entityPathsInWorkspot.Size() );

	String filename = workspot.GetPath().ToString().StringAfterFromRight("\\");
	Bool isPlayerWorkspot = filename.BeginsWith("player");

	for ( const CName& entityPathInWorkspot : entityPathsInWorkspot )
	{
		if( m_safeMode && !entityPathInWorkspot.AsStringView().StartsWith("base\\characters\\base_entities") )//safe mode
		{
			m_failedResourceMessages.PushBack(red::String::Printf( "Workspot resource '%hs' cannot be processed by '%s' commandlet. Because safe mode is enabled, skipping processing whole workspot", workspot.GetPath().ToDebugString(), GetName().AsChar() ).AsChar());
			return false;
		}
		resourcePaths.PushBack( res::ResourcePath::Build( entityPathInWorkspot.AsChar() ) );
	}

	const red::DynArray< AnsiChar* >& paths = tools::GetEditorEntitiesPaths();

	for (const AnsiChar* entityPath : paths)
	{
		red::alg::PushBackUnique(resourcePaths, res::ResourcePath::Build(entityPath));
	}

	m_animSetCollectorCache->AddResourcePaths( resourcePaths );

	//loop through all supported rigs and collect the anim sets
	red::DynArray<THandle< anim::AnimSet >> animSets;
	for (const res::ResourcePath& path : resourcePaths)
	{
		auto& animSetEntry = m_animSetCollectorCache->GetRawWorkspotAnimsetEntry(path);

		for (const auto& animSetup : animSetEntry.m_animations.m_gameplay)
		{
			if (m_failedAnimSet.Exist(animSetup.m_animSet.GetPath()))
			{
				continue;
			}

			if(const auto& animSet = GetAnimSet(animSetup.m_animSet))
			{
				animSets.PushBack(animSet);
			}
			else 
			{
				m_failedAnimSet.Insert(animSetup.m_animSet.GetPath());
				RED_LOG_ERROR( "Anim Set resource '%hs' cannot be processed by '%s' commandlet.", animSetup.m_animSet.GetPath().ToDebugString(), GetName().AsChar() );
			}
		}
		for (const auto& animSetup : animSetEntry.m_animations.m_cinematics)
		{
			if (m_failedAnimSet.Exist(animSetup.m_animSet.GetPath()))
			{
				continue;
			}

			if(const auto& animSet = GetAnimSet(animSetup.m_animSet))
			{
				animSets.PushBack(animSet);
			}
			else 
			{
				m_failedAnimSet.Insert(animSetup.m_animSet.GetPath());
				RED_LOG_ERROR( "Anim Set resource '%hs' cannot be processed by '%s' commandlet.", animSetup.m_animSet.GetPath().ToDebugString(), GetName().AsChar() );
			}
		}
	}

	//include final animsets
	for (const auto& animSetEntry : workspot.m_workspotTree->m_finalAnimsets) 
	{
		for (const auto& animSetup : animSetEntry.m_animations.m_gameplay)
		{
			if (m_failedAnimSet.Exist(animSetup.m_animSet.GetPath()))
			{
				continue;
			}

			if(const auto& animSet = GetAnimSet(animSetup.m_animSet))
			{
				animSets.PushBack(animSet);
			}
			else 
			{
				m_failedAnimSet.Insert(animSetup.m_animSet.GetPath());
				RED_LOG_ERROR( "Anim Set resource '%hs' cannot be processed by '%s' commandlet.", animSetup.m_animSet.GetPath().ToDebugString(), GetName().AsChar() );
			}
		}
		for (const auto& animSetup : animSetEntry.m_animations.m_cinematics)
		{
			if (m_failedAnimSet.Exist(animSetup.m_animSet.GetPath()))
			{
				continue;
			}

			if(const auto& animSet = GetAnimSet(animSetup.m_animSet))
			{
				animSets.PushBack(animSet);
			}
			else 
			{
				m_failedAnimSet.Insert(animSetup.m_animSet.GetPath());
				RED_LOG_ERROR( "Anim Set resource '%hs' cannot be processed by '%s' commandlet.", animSetup.m_animSet.GetPath().ToDebugString(), GetName().AsChar() );
			}
		}
	}

	//get all container sequences to be processed
	//by design we only care about the first level from root or second level if sequence is inside conditional sequence
	THandle<work::IContainerEntry> root = Cast<work::IContainerEntry>(workspot.m_workspotTree->m_rootEntry);

	red::DynArray<THandle<work::IContainerEntry>> processedSequencesContainer;
	processedSequencesContainer.PushBack(root);

	for (const auto& entry : root->m_list) 
	{
		if (entry->IsExactlyA<work::ConditionalSequence>()) 
		{
			processedSequencesContainer.PushBack(Cast<work::IContainerEntry>(entry));
		}
	}

	//get all sequences and idle animations
	red::DynArray<THandle<work::IContainerEntry>> sequences;

	//go through all processed container and collect all sequence
	for (const auto& processed : processedSequencesContainer) 
	{ 
		for (const auto& entry : processed->m_list) 
		{
			if (THandle<work::IContainerEntry>& sequence = Cast<work::IContainerEntry>(entry))
			{
				if(sequence->m_idleAnim != CName::NONE())
				{
					//register idle animation for each sequence
					sequences.PushBack(sequence);
				}
			}
		}
	}

	Uint32 reactionGenerated = 0;

	if( m_generateReaction && !isPlayerWorkspot )
	{
		reactionGenerated = AddReactionSequences( workspot, animSets );
	}

	//for each idle anim create entry node in root
	Uint32 entryGenerated = 0;
	Uint32 exitGenerated = 0;
	if( m_purgeGates )
	{
		RemoveExisingGates( workspot );
	}
	
	if ( m_generateGates )
	{
		for (auto sequence : sequences)
		{
			entryGenerated += AddEntryAnimations(workspot, sequence, animSets);
			exitGenerated += AddExitAnimations(workspot, sequence, animSets);
		}
	}
	if ( m_generateFastExits || m_generateGates )
	{
		for( auto sequence : sequences )
		{
			exitGenerated += AddFastExitAnimations( workspot, sequence, animSets );
		}
	}
	Uint32 bumpsGenerated = 0;
	if( m_generateBumps && !isPlayerWorkspot )
	{
		bumpsGenerated = AddBumpSequences( workspot, animSets );
	}

	//update final animsets
	red::DynArray< res::ResourcePath > acceptedEntities{ red::PoolBackend() };
	tools::GenerateFinalAnimSets(*m_animSetCollectorCache, resourcePaths, HandleFromPtr( &workspot ), workspot.m_workspotTree,&acceptedEntities, nullptr,"");
	workspot.m_workspotTree->CacheAnimsetHandles();
	//save
	workspot.m_workspotTree->m_rootEntry->NotifyPropertyChanged( rtti::AccessPath() );

	m_successResourceMessages.PushBack(red::String::Printf("Workspot resource '%hs' proccessed. entry added: %u, exit added: %u,reaction added: %u, bumps added %u ",workspot.GetPath().ToDebugString(), entryGenerated, exitGenerated, reactionGenerated, bumpsGenerated ).AsChar());
	return true;
}
