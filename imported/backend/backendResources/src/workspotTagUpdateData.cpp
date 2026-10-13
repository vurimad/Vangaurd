/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resaveCommandlet.h"

#include "../../backendEngine/include/commandletRenderInstance.h"
#include "../../../common/gameWorkspots/include/workspotAnimSetCollector.h"
#include "../../../common/redReflection/include/gatheredResource.h"

struct DataEntry
{
	res::ResourcePath path;
	red::TagList blacklist;
	red::TagList whitelist;
};

class WorkspotTagResaveCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( WorkspotTagResaveCommandlet );
public:
	WorkspotTagResaveCommandlet();
	virtual ~WorkspotTagResaveCommandlet() = default;

	virtual const AnsiChar* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;
	virtual Bool InitializeEnvironment() override final;
	virtual void TeardownEnvironment() override final;

private:
	virtual Bool OnResourcePreSave( CResource& resource ) override final;
	Bool ProcessWorkspot( work::WorkspotResource& workspot );

	commandlet::RenderInstance m_renderInstance;


	// Output of script processing of:
	// https://docs.google.com/spreadsheets/d/1jrAPIsjzSvyUYFe-e5eK1bqFO4mIcAZ8CemHO4-TSUY/edit#gid=0
	red::DynArray<DataEntry> m_dataList;
};

RTTI_BEGIN_TYPE( WorkspotTagResaveCommandlet )
RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()


WorkspotTagResaveCommandlet::WorkspotTagResaveCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "workspotTagResaveComandlet" ) )
{
}

const AnsiChar* WorkspotTagResaveCommandlet::GetOneLiner() const
{
	return nullptr;
}

void WorkspotTagResaveCommandlet::PrintHelp() const
{
}

namespace helper
{
	red::TagList SplitTags( String string )
	{
		red::TagList res;
		red::DynArray< String > tokens =  string.Split( "," );

		for( String& s : tokens )
		{
			s.Trim();
			res.AddTag( RED_NAME( s ) );
		}

		return res;
	}
}


Bool WorkspotTagResaveCommandlet::InitializeEnvironment()
{
	Bool result = ResaveCommandlet::InitializeEnvironment(); // && m_renderInstance.Initialise();
	
	String dataPath = "test/commandlet/workspot_tag_resave/tag_update_list.csv";

	auto configCsv = Cast< C2dArray >( GResourceLoader->IssueLoadingRequest( res::ResourcePath::Build( dataPath ) )->WaitUntilLoaded() );
	if( !configCsv )
	{
		RED_LOG_ERROR( "Could not load commandlet data file - %s ", dataPath.AsChar() );
		return false;
	}

	m_dataList.Reserve( configCsv->GetNumberOfRows() );
	for( Uint32 i = 0; i < configCsv->GetNumberOfRows(); ++i )
	{
		String workspotPath = configCsv->GetValueRef( 0, i );
		String blacklist = configCsv->GetValueRef( 1, i );
		String whitelist = configCsv->GetValueRef( 2, i );
		
		m_dataList.PushBack( { res::ResourcePath::Build( workspotPath ), helper::SplitTags( blacklist ), helper::SplitTags( whitelist ) } );
	}

	return result;
}

void WorkspotTagResaveCommandlet::TeardownEnvironment()
{
	//m_renderInstance.Shutdown();
	ResaveCommandlet::TeardownEnvironment();
}

namespace helper
{
	void MakeUpperCase( red::TagList& list )
	{
		red::TagList copy = list;
		for ( Uint32 i = 0; i< copy.Size(); i++ )
		{
			CName tag = copy[i];
			const red::StringView name = tag.AsStringView();
			if ( !name.Empty() )
			{
				if( std::islower( static_cast< unsigned char >( name[0] ) ) )
				{
					String newTag = name.ToString();
					newTag[0] = static_cast< char >( std::toupper( static_cast< unsigned char >( newTag[0] ) ) );
					list.RemoveTag( tag );
					list.AddTag( RED_NAME( newTag ) );
				}				
			}
		}
	}
}

Bool WorkspotTagResaveCommandlet::ProcessWorkspot( work::WorkspotResource& workspot )
{
	auto iter = std::find_if( m_dataList.Begin(), m_dataList.End(), [&workspot]( const DataEntry& gEntry )
	{
		return gEntry.path == workspot.GetPath();
	} );

	if ( iter != m_dataList.End() )
	{
		red::TagList addWhitelist( iter->whitelist );
		workspot.m_workspotTree->EDITOR_WhitelistForEdit().AddTags( addWhitelist );
		
		red::TagList addBlackList( iter->blacklist );
		workspot.m_workspotTree->EDITOR_BlackListForEdit().AddTags( addBlackList );
	}
	else
	{
		RED_LOG_INFO( "Resource not on the list - only case sensitivity - %s", workspot.GetPath().ToDebugString() );
	}

	helper::MakeUpperCase(workspot.m_workspotTree->EDITOR_BlackListForEdit());
	helper::MakeUpperCase(workspot.m_workspotTree->EDITOR_WhitelistForEdit());

	return true;
}

Bool WorkspotTagResaveCommandlet::OnResourcePreSave( CResource& resource )
{
	if ( resource.IsA< work::WorkspotResource >() )
	{
		return ProcessWorkspot( static_cast< work::WorkspotResource& >( resource ) );
	}
	
	RED_LOG_ERROR( "Resource '%hs' cannot be processed by workspotResave commandlet.", resource.GetPath().ToDebugString() );
	return false;
}
