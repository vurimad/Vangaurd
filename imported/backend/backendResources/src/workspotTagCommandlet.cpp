/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resaveCommandlet.h"
#include "../../../common/gameWorkspots/include/workspotResource.h"
#include "../../../common/redCore/include/commandline.h"

class WorkspotTagCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( WorkspotTagCommandlet );
public:
	WorkspotTagCommandlet();

	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void TeardownEnvironment() override;

private:
	virtual Bool OnResourcePreSave( CResource& resource ) override final;
	Bool ProcessWorkspot( work::WorkspotResource& workspot );

	red::DynArray<String> m_tagsToAddString { red::PoolBackend() };
	red::DynArray<String> m_tagsToRemoveString { red::PoolBackend() };

	red::DynArray<CName> m_tagsToAdd { red::PoolBackend() };
	red::DynArray<CName> m_tagsToRemove { red::PoolBackend() };

	red::DynArray<red::String> m_logMessages { red::PoolBackend() };
	red::DynArray<red::String> m_errorMessages { red::PoolBackend() };

	Bool m_fatalError = false;
};


RTTI_BEGIN_TYPE( WorkspotTagCommandlet )
RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

WorkspotTagCommandlet::WorkspotTagCommandlet()
	: ResaveCommandlet(RED_NAME_CONSTEXPR("workspotTag"))
{
}

bool WorkspotTagCommandlet::Execute(const red::CommandLine & options)
{


	Bool tagsSanityCheckPassed = true;

	static const String addOptionName = "add";
	if( options.HasOption(addOptionName) )
	{
		const red::DynArray<String>& addTags = options.GetValues(addOptionName);
		for ( const String& tag : addTags )
		{
			if (red::alg::PushBackUnique(m_tagsToAdd, RED_NAME(tag))) 
			{
				m_tagsToAddString.PushBack(tag);
			}
		}
	}
	else
	{
		m_errorMessages.PushBack(red::String::Printf( "No argument '%hs' found, no tags will be added", addOptionName ));
	}

	static const String removeOptionName = "remove";
	if( options.HasOption(removeOptionName) )
	{
		const red::DynArray<String>& removedTags = options.GetValues(removeOptionName);
		for ( const String& tag : removedTags )
		{
			CName tagToRemove = RED_NAME(tag);
			if (m_tagsToAdd.Exist(tagToRemove))
			{
				m_errorMessages.PushBack(red::String::Printf( "Commandlet will not run because tag '%hs' is in add and remove list.", tag.AsChar() ));
				tagsSanityCheckPassed = false;
			}

			if (red::alg::PushBackUnique(m_tagsToRemove, tagToRemove))
			{
				m_tagsToRemoveString.PushBack(tag);
			}
		}
	}
	else
	{
		m_errorMessages.PushBack(red::String::Printf( "No argument '%hs' found, no tags will be removed", removeOptionName ));
	}

	if ( m_tagsToAdd.Size() == 0 && m_tagsToRemove.Size() == 0 )
	{
		m_errorMessages.PushBack("Commandlet will not run because there's no tags to add or remove.");
		tagsSanityCheckPassed = false;
	}

	if ( !tagsSanityCheckPassed )
	{
		//terminate the commandlet without processing assets if data is wrong
		m_fatalError = true;
		return false;
	}

	return ResaveCommandlet::Execute(options);
}

void WorkspotTagCommandlet::TeardownEnvironment()
{
	RED_LOG_INFO("                    ________________________");
	RED_LOG_INFO("                   | W O R K S P O T  T A G |");
	RED_LOG_INFO("                   |   C O M M A N D L E T  |");
	RED_LOG_INFO("                   |________________________|");
	RED_LOG_INFO("n___n");//long cat is here to help users read the logs
	if (m_fatalError)
	{
		RED_LOG_INFO("|x x|");
	}
	else
	{
		RED_LOG_INFO("|O O|  *Meow*");
	}
	RED_LOG_INFO("| ^ |");
	RED_LOG_INFO("|0o0|--------------------- Parameters --------------------------" );
	RED_LOG_INFO("|u u| Tags to add:");
	for ( const auto& msg : m_tagsToAddString ) 
	{
		RED_LOG_INFO("|   |   - [%hs]",msg.AsChar() );
	}

	RED_LOG_INFO("|   | Tags to remove:");
	if ( m_tagsToRemoveString.Size() > 0 ) 
	{
		for ( const auto& msg : m_tagsToRemoveString ) 
		{
			RED_LOG_INFO("|   |   - [%hs]",msg.AsChar() );
		}
	}

	if( m_errorMessages.Size() > 0 )
	{
		//error section
		RED_LOG_ERROR("|---|--------------------- Errors --------------------------" );
		for ( const auto& msg : m_errorMessages ) 
		{
			RED_LOG_ERROR("| o | %hs", msg.AsChar() );
		}
		RED_LOG_ERROR("|___|");
	}

	if( m_logMessages.Size() > 0 )
	{
		RED_LOG_INFO("|---|--------------------- Logs --------------------------" );
		//generic section
		for ( const auto& msg : m_logMessages ) 
		{
			RED_LOG_INFO("|   | %hs", msg.AsChar() );
		}
	}

	RED_LOG_INFO("|___|--------------------- End of message --------------------------" );
	ResaveCommandlet::TeardownEnvironment();
}

Bool WorkspotTagCommandlet::OnResourcePreSave(CResource& resource)
{
	if ( resource.IsA< work::WorkspotResource >() )
	{
		return ProcessWorkspot( static_cast< work::WorkspotResource& >( resource ) );
	}

	m_errorMessages.PushBack(red::String::Printf( "Resource '%hs' cannot be processed by '%s' commandlet.", resource.GetPath().ToDebugString(), GetName().AsChar() ));
	return false;
}

Bool WorkspotTagCommandlet::ProcessWorkspot(work::WorkspotResource& workspot)
{
	red::StringBuilder<String> addedTags;
	red::StringBuilder<String> removedTags;

	for ( Uint32 i = 0; i < m_tagsToRemove.Size(); ++i )
	{
		const CName tag = m_tagsToRemove[i];
		if (workspot.m_workspotTree->m_tags.HasTag(tag))
		{
			removedTags.Append(red::String::Printf("[%hs]", m_tagsToRemoveString[i].AsChar()));
			workspot.m_workspotTree->m_tags.RemoveTag(tag);
		}
	}

	for ( Uint32 i = 0; i < m_tagsToAdd.Size(); ++i )
	{
		const CName tag = m_tagsToAdd[i];
		if ( !workspot.m_workspotTree->m_tags.HasTag(tag) )
		{
			addedTags.Append(red::String::Printf("[%hs]", m_tagsToAddString[i].AsChar()));
			workspot.m_workspotTree->m_tags.AddTag(tag);
		}
	}

	if ( !addedTags.Empty() || !removedTags.Empty() )
	{
		m_logMessages.PushBack(red::String::Printf( "Resource '%hs'.", workspot.GetPath().ToDebugString() ));

		if( !addedTags.Empty() )
		{
			m_logMessages.PushBack(red::String::Printf( "     Added tags: %hs.", addedTags.AsChar()));
		}

		if( !removedTags.Empty() )
		{
			m_logMessages.PushBack(red::String::Printf( "   Removed tags: %hs.", removedTags.AsChar() ));
		}
	}

	

	if ( workspot.m_workspotTree->m_tags.Size() > 0)
	{
		if (addedTags.Empty() && removedTags.Empty())
		{
			m_logMessages.PushBack(red::String::Printf( "Resource '%hs'.", workspot.GetPath().ToDebugString() ));
		}

		red::StringBuilder<String> currentTags;

		for (auto& tag : workspot.m_workspotTree->m_tags.GetTagNames())
		{
			currentTags.Append(red::String::Printf("[%hs]", tag.AsChar()));
		}

		m_logMessages.PushBack(red::String::Printf( "   Current tags: %hs.", currentTags.AsChar() ));
	}

	return true;
}
