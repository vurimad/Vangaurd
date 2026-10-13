/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redContainers/include/string/stringBuilder.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/issueReporter/include/jiraService.h"
#include "../../../common/issueReporter/include/jiraDataStruct.h"
#include "../../../common/redReflection/include/version.h"

namespace tools
{
	class DumpNonProdAssetsCmdlet : public red::ICommandlet
	{
		RTTI_DECLARE_TYPE( DumpNonProdAssetsCmdlet );

	public:
		DumpNonProdAssetsCmdlet();

		Bool InitializeEnvironment();
		void TeardownEnvironment();

		virtual Bool Execute( const red::CommandLine& options ) override;
		virtual const char* GetOneLiner() const override;
		virtual void PrintHelp() const override {}

	private:
		void Process( const res::ResourcePath& path );
		void SaveRaport();
		void AppendResourceInfo( red::StringBuilder<String>& outSb, red::DynArray< res::ResourcePath >& outMiddlemen, const res::ResourcePath& resourcePath, const res::ResourcePath& middleman = res::ResourcePath() );
		void CreateJira( const red::DynArray< res::ResourcePath >& nonProdAssets, const res::ResourcePath& parentResource );

		Bool m_createJiraIssues = false;
		red::AbsolutePath m_reportPath;

		commandlet::EngineInstance m_engineInstance;
		resdb::Database m_database;
		JiraService m_jiraService;

		res::ResourceEditorDepot* m_sourceDepot{};

		struct References
		{
			red::DynArray< res::ResourcePath > m_nonProdRefs;
			red::DynArray< res::ResourcePath > m_baseRefs;
		};
		red::HashMap< res::ResourcePath, References > m_references;
		red::DynArray< res::ResourcePath > m_toCheckArray;
		red::Set< res::ResourcePath > m_toCheck;

		red::Set< res::ResourcePath > m_initialResources;
		red::Set< res::ResourcePath > m_reported;
	};

	DumpNonProdAssetsCmdlet::DumpNonProdAssetsCmdlet()
		: red::ICommandlet( RED_NAME_CONSTEXPR( "validateProductionData" ) )
	{
	}

	Bool DumpNonProdAssetsCmdlet::InitializeEnvironment()
	{
		commandlet::BackendDepotHolder depotHolder;

		red::AbsolutePath resdbCachePath = depotHolder.GetCachePath().AddFilePath( resdb::Database::GetDefaultFilename() );
		m_database.Initialise( *depotHolder.GetDepot(), resdbCachePath );

		m_sourceDepot = commandlet::GetEditorDepot();
		if ( !m_sourceDepot )
		{
			return false;
		}
		return m_engineInstance.Initialise();
	}

	void DumpNonProdAssetsCmdlet::TeardownEnvironment()
	{
		m_engineInstance.Shutdown();
	}

	void DumpNonProdAssetsCmdlet::Process( const res::ResourcePath& resourcePath )
	{
		if ( m_references.KeyExist( resourcePath ) )
		{
			return;
		}
		m_references.Insert( resourcePath, References() );

		resdb::Links references = m_database.GetForwardLinks( resourcePath );
		for ( const resdb::Link& reference : references )
		{
			if ( !reference.to.ToString().BeginsWith( "base" ) && !reference.to.ToString().BeginsWith( "engine" ) )
			{
				m_references[resourcePath].m_nonProdRefs.PushBack( reference.to );
			}
			else
			{
				m_references[resourcePath].m_baseRefs.PushBack( reference.to );
				if ( !m_references.KeyExist( reference.to ) )
				{
					if ( !m_toCheck.Exist( reference.to ) )
					{
						m_toCheck.Insert( reference.to );
						m_toCheckArray.PushBack( reference.to );
					}
				}
			}
		}
	}

	Bool DumpNonProdAssetsCmdlet::Execute( const red::CommandLine& options )
	{
		String path;
		if ( !options.GetFirstParam( "outputFile", path ) )
		{
			return false;
		}

		m_reportPath = red::AbsolutePath::ParseFilePath( path );
		if ( !m_reportPath.IsFilePath() || !red::paths::ExtractExtension( m_reportPath ).EqualsNC( "csv" ) )
		{
			return false;
		}

		if ( options.HasOption( "enableJiraReporting" ) )
		{
			m_createJiraIssues = true;
			m_jiraService.SetBasicAuthorization( "aXNzdWVyZXBvcnRlcjpNSEdyVHZMNVcxR2VmV2FLWVphSA==" );
		}

		red::DynArray< res::ResourcePath > resources = commandlet::ParseResourcePathsFromCommandLine( *m_sourceDepot, options );
		for ( const res::ResourcePath& resourcePath : resources )
		{
			Process( resourcePath );
			m_initialResources.Insert( resourcePath );
		}

		for ( Uint32 i = 0; i < m_toCheckArray.Size(); ++i )
		{
			const res::ResourcePath path = m_toCheckArray[i];
			Process( path );
		}

		SaveRaport();
		return true;
	}

	const char* DumpNonProdAssetsCmdlet::GetOneLiner() const
	{
		return "Dumps list of non production assets referenced in production resources";
	}

	void DumpNonProdAssetsCmdlet::SaveRaport()
	{
		red::StringBuilder<String> sb;
		sb.Append( "Asset path;Non prod reference;Through;\n" );

		for ( auto res : m_initialResources )
		{
			red::DynArray< res::ResourcePath > middlemen;
			AppendResourceInfo( sb, middlemen, res );

			for ( Uint32 i = 0; i < middlemen.Size(); ++i )
			{
				AppendResourceInfo( sb, middlemen, res, middlemen[i] );
			}
		}

		if ( !red::SaveStringToFile( m_reportPath, sb.ToString() ) )
		{
			RED_LOG_ERROR( "Failed to save report file '%hs'", m_reportPath.ToDebugString() );
		}
	}

	void DumpNonProdAssetsCmdlet::AppendResourceInfo( red::StringBuilder<String>& outSb, red::DynArray< res::ResourcePath >& middlemen, const res::ResourcePath& resourcePath, const res::ResourcePath& middleman )
	{
		const res::ResourcePath toCheck = middleman.IsEmpty() ? resourcePath : middleman;
		if ( !m_references[toCheck].m_nonProdRefs.Empty() )
		{
			for ( const res::ResourcePath& nonProdRef : m_references[toCheck].m_nonProdRefs )
			{
				outSb.Append( String::Printf( "%hs;%hs;%hs;\n", resourcePath.ToString().AsChar(),
					nonProdRef.ToString().AsChar(), middleman.ToString().AsChar() ) );
			}
			CreateJira( m_references[toCheck].m_nonProdRefs, toCheck );
		}

		for ( const res::ResourcePath& ref : m_references[toCheck].m_baseRefs )
		{
			if ( m_initialResources.Exist( ref ) )
			{
				continue;
			}

			if ( middlemen.End() != std::find_if( middlemen.Begin(), middlemen.End(),
				[ref] ( const res::ResourcePath& path )
			{
				return path == ref;
			} ) )
			{
				continue;
			}
			middlemen.PushBack( ref );
		}
	}

	void DumpNonProdAssetsCmdlet::CreateJira( const red::DynArray< res::ResourcePath >& nonProdAssets, const res::ResourcePath& parentResource )
	{
		if ( !m_createJiraIssues || m_reported.Exist( parentResource ) )
		{
			return;
		}

		THandle< JiraCreateIssueBody > createIssueBody = CreateHandle< JiraCreateIssueBody >();
		createIssueBody->m_fields.m_summary = String::Printf( "[NonProdAssets] %hs uses non prod assets", parentResource.ToDebugString() );

		createIssueBody->m_fields.m_description = String::Printf( "%hs references non production assets:\n", parentResource.ToDebugString() );
		for ( const res::ResourcePath& path : nonProdAssets )
		{
			createIssueBody->m_fields.m_description += path.ToString();
			createIssueBody->m_fields.m_description += "\n";
		}

		createIssueBody->m_fields.m_project = tools::JiraProject{ "CYB" };
		createIssueBody->m_fields.m_issuetype = tools::JiraIssueType{ "Bug" };
		createIssueBody->m_fields.m_priority = tools::JiraPriority{ "P1" };
		createIssueBody->m_fields.m_assetBrowserPath = parentResource.ToString();
		createIssueBody->m_fields.m_assignee = tools::JiraCustomFieldName{ "jowita.hacia" };

		/*createIssueBody->m_fields.m_cameraLocation = red::String::Printf(
			"{ position = {X = %f,Y = %f,Z = %f},rotation = {Pitch = 0.000000,Yaw = 0.000000,Roll = 0.000000},orbitDistance = 1.000000,zoom = 50.000000 }",
		);*/
		createIssueBody->m_fields.m_frequency = tools::JiraCustomFieldValue{ "Always" };
		createIssueBody->m_fields.m_fixVersions.PushBack( tools::JiraFixVersion{ "14314" } ); // Backlog
		createIssueBody->m_fields.m_optimization = tools::JiraCustomFieldValue{ "CONTENT" };
		createIssueBody->m_fields.m_buildVersion = APP_VERSION_NUMBER;

		m_jiraService.CreateIsssue(
			createIssueBody,
			[this, parentResource] ( Bool success, const THandle< tools::JiraCreateIssueResult >& jiraResult )
		{
			if ( success )
			{
				m_reported.Insert( parentResource );
				RED_LOG_INFO( "[JIRA] NonProdAsset jira created: '%hs'", jiraResult->m_key.AsChar() );
			}
			else
			{
				RED_LOG_ERROR( "[JIRA] Failed to create NonProdAsset jira" );
			}
		} );
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( DumpNonProdAssetsCmdlet, tools )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()