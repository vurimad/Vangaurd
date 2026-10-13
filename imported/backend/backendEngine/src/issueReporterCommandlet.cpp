/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../common/core/include/commandlet.h"
#include "../../common/issueReporter/include/jiraService.h"
#include "../../common/issueReporter/include/jiraDataStruct.h"

class IssueReporterCommandlet: public red::ICommandlet
{
	RTTI_DECLARE_TYPE( IssueReporterCommandlet );

public:
	IssueReporterCommandlet();
	~IssueReporterCommandlet();

	// ICommandlet interface
	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
	virtual void PrintHelp() const override;
	virtual const char* GetOneLiner() const override;
	virtual bool Execute( const red::CommandLine& options ) override;
};

RTTI_BEGIN_TYPE( IssueReporterCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

IssueReporterCommandlet::~IssueReporterCommandlet() = default;
IssueReporterCommandlet::IssueReporterCommandlet()
	: ICommandlet( RED_NAME( "issue-reporter" ) )
{

}


Bool IssueReporterCommandlet::InitializeEnvironment()
{
	return true;
}

void IssueReporterCommandlet::TeardownEnvironment()
{

}

void IssueReporterCommandlet::PrintHelp() const
{

}

const char* IssueReporterCommandlet::GetOneLiner() const
{
	return "Issue Reporter Tests";
}

bool IssueReporterCommandlet::Execute( const red::CommandLine& options )
{
	ed::JiraService service;

	THandle< ed::JiraCreateIssueBody > request = CreateHandle< ed::JiraCreateIssueBody >();
	request->m_fields.m_summary = "Test Jira Issue Creation Please";
	request->m_fields.m_project.m_key = "RCS";
	request->m_fields.m_description = "No description here :D";
	request->m_fields.m_issuetype.m_name = "Bug";
	request->m_fields.m_priority.m_name = "P3";

	service.CreateIsssue( request, [ &service ] ( Bool success, THandle< ed::JiraCreateIssueResult > result) 
	{
		if ( success && result )
		{
			String path = "C:\\Users\\przemyslaw.banasiak\\Downloads\\Default_selected.png";
			String pathB = "C:\\Users\\przemyslaw.banasiak\\Downloads\\textAtt.txt";
			red::DynArray< red::AbsolutePath > attachmentPaths = { red::AbsolutePath::CreateFilePath( path ), red::AbsolutePath::CreateDirPath( pathB ) };
			service.AddAttachments( result->m_key, attachmentPaths, [ &service ] ( Bool, THandle< ed::JiraAddAttachmentsResult > result ) 
			{
				if ( result )
				{
					RED_LOG_INFO( "Added %d attachments to the issue ", result->m_array.Size() );
				}
				else
				{
					RED_LOG_ERROR( "Adding attacments: Something went no-yes" );
				}

				service.SearchIssues( "project=RCS", "status", [] ( Bool, THandle< ed::JiraSearchIssuesResult > result ) 
				{
					if ( result )
					{
						RED_LOG_INFO( "There are %d issues in project", result->m_total );
					}
					else
					{
						RED_LOG_ERROR( "Searching issues: Something went no-yes" );
					}
				} );
			} );
		}
		else
		{
			RED_LOG_ERROR( "Adding issue: Something went no-yes" );
		}
	} );

	return true;
}

