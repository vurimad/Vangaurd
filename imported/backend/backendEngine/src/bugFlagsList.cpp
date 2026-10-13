/**
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "bugFlagsList.h"

#include "../../backendEngine/include/interopList.h"
#include "../../backendEngine/include/resourceEditor.h"
#include "../../../common/issueReporter/include/jiraService.h"

#include "../../common/generalEditorBackend/include/bugFlagsIssueNodeData.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( BugFlagsIssueNode, tools )
RTTI_PARENT_TYPE( utils::NamedNode )
RTTI_END_TYPE();

namespace tools
{
	void AppendJqlCondition( String& jql, const String& condition )
	{
		if ( !jql.Empty() )
		{
			jql += " AND ";
		}
		jql += condition;
	}

	//////////////////////////////////////////////////////////////////////////

	BugFlagsIssueNode::BugFlagsIssueNode()
		: BugFlagsIssueNode( JiraIssue(), nullptr )
	{
	}

	BugFlagsIssueNode::BugFlagsIssueNode( const JiraIssue& data, utils::SimpleTree* parentTree )
		: TBaseClass( data.m_fields.m_summary, parentTree )
		, m_data( data )
	{
		OnDataChange();
	}

	THandle< tools::TreeNodeData > BugFlagsIssueNode::CreateNodeDataStruct() const
	{
		return CreateHandle< BugFlagsIssueNodeData >();
	}

	void BugFlagsIssueNode::FillNodeDataStruct( const THandle< TreeNodeData >& nodeData ) const
	{
		TBaseClass::FillNodeDataStruct( nodeData );

		if ( THandle< BugFlagsIssueNodeData > data = Cast< BugFlagsIssueNodeData >( nodeData ) )
		{
			data->m_description = m_data.m_fields.m_description;
			data->m_key = m_data.m_key;
			data->m_labels = m_data.m_fields.m_labels;
			data->m_priority = m_data.m_fields.m_priority.m_name;
			data->m_project = m_data.m_fields.m_project.m_name;
		}
	}

	void BugFlagsIssueNode::SetNodeDataStruct( const THandle<TreeNodeData>& data )
	{
		TBaseClass::SetNodeDataStruct( data );
	}

	String BugFlagsIssueNode::ToString() const
	{
		red::StringBuilder<String> builder;
		builder.Append( m_data.m_fields.m_summary );

		return builder.ToString();
	}

	void BugFlagsIssueNode::SetJiraIssue( const JiraIssue& value )
	{
		m_data = value;
		ChangeName( value.m_fields.m_summary );

		OnDataChange();

		m_treeFallbackPtr->NotifyNodeInvalidated( GetId() );
	}

	void BugFlagsIssueNode::OnDataChange()
	{
		const String& positionString = m_data.m_fields.m_flagPosition;

		m_flagPosition.X = FromStringDirect< Float >( positionString.StringAfter( "[" ).StringBefore( " " ) );
		m_flagPosition.Y = FromStringDirect< Float >( positionString.StringAfter( " " ).StringBefore( " " ) );
		m_flagPosition.Z = FromStringDirect< Float >( positionString.StringAfterFromRight( " " ).StringBefore( "]" ) );

		const String& priority = m_data.m_fields.m_priority.m_name;
		if ( priority == "P0" )
		{
			m_color = Color::RED();
		}
		else if ( priority == "P1" )
		{
			m_color = Color::ORANGE();
		}
		else if ( priority == "P2" )
		{
			m_color = Color::YELLOW();
		}
		else if ( priority == "P3" )
		{
			m_color = Color::LIGHT_GREEN();
		}
		else
		{
			m_color = Color::DARK_GREEN();
		}
	}

	//////////////////////////////////////////////////////////////////////////

	BugFlagsTree::BugFlagsTree( const res::ResourcePath& resourcePath )
		: m_resourcePath( resourcePath )
	{
		red::SharedPtr< utils::SimpleNode > rootNode = red::CreateSharedPtr< utils::SimpleNode >();
		SetRoot( rootNode );
		SetRootHidden( true );
	}

	void BugFlagsTree::FillTree( const String& jql )
	{
		Clear();

		String pathString = m_resourcePath.ToString();
		pathString.ReplaceAll( "\\", "\\\\" );

		String fullJql = String::Printf( "\"Bugs component\"=flag AND \"Asset Browser Path\" ~ %hs AND Status != Closed", pathString.AsChar() );
		if ( !jql.Empty() )
		{
			fullJql += " AND ";
			fullJql += jql;
		}

		JiraService service;
		service.SetOAuthToken( m_jiraOauth );
		service.SearchIssues( fullJql, "summary,project,description,priority,customfield_24800,customfield_10013,customfield_18373,customfield_15306,customfield_10505,fixVersions,attachment,assignee,customfield_25500",
			[this] ( Bool, THandle< JiraSearchIssuesResult > result )
			{
				if ( result == nullptr )
				{
					return;
				}

				for ( const JiraIssue& issue : result->m_issues )
				{
					red::SharedPtr< utils::NamedNode > issueNode = red::CreateSharedPtr< BugFlagsIssueNode >( issue, this );
					AddNodeToRoot( issueNode );
				}
			} );
	}

	void BugFlagsTree::FilterNodes( const String& filterString )
	{
		FillTree( String::Printf( "Text ~\"%hs\"", filterString.AsChar() ) );
	}

	void BugFlagsTree::FilterNodes( const red::HashMap<String, String>& filtersMap )
	{
		String jql;

		const String* text = filtersMap.FindPtr( "text" );
		if ( text && !( *text ).Empty() )
		{
			AppendJqlCondition( jql, String::Printf( "Text ~\"%hs\"", ( *text ).AsChar() ) );
		}

		const String* assignee = filtersMap.FindPtr( "assignee" );
		if ( assignee && !( *assignee ).Empty() )
		{
			String validatedAssignee = *assignee;
			validatedAssignee.ReplaceAll( " ", "." );
			AppendJqlCondition( jql, String::Printf( "Assignee = \"%hs\"", ( *assignee ).AsChar() ) );
		}

		const String* project = filtersMap.FindPtr( "project" );
		if ( project && !( *project ).Empty() )
		{
			AppendJqlCondition( jql, String::Printf( "Project =%hs", ( *project ).AsChar() ) );
		}

		const String* priority = filtersMap.FindPtr( "priority" );
		if ( priority && !( *priority ).Empty() )
		{
			AppendJqlCondition( jql, String::Printf( "Priority =%hs", ( *priority ).AsChar() ) );
		}

		FillTree( jql );
	}

	void BugFlagsTree::SetOAuthToken( const oauth::AuthenticationToken &token )
	{
		m_jiraOauth = token;
	}

	const tools::TreeNodeID BugFlagsTree::AllocateNodeId( const ITreeNode& node )
	{
		if ( node.IsA<BugFlagsIssueNode >() )
		{
			const BugFlagsIssueNode* issueNode = static_cast<const BugFlagsIssueNode*>( &node );
			return FromStringDirect<TreeNodeID>( issueNode->GetJiraIssue().m_id );
		}

		const ITreeNode* root = GetNode( 0 );
		RED_FATAL_ASSERT( root == nullptr, "Only root is not a BugFlagsIssueNode" );

		return 0;
	}

	void BugFlagsTree::SetupInterop( BugFlagsTree* ptr, ::interop::utils::Object& op )
	{
		op.RegisterFunctor<void>( "editSelection", [ ptr ]( interop::IContext& ctx )
		{
			const red::HashSet< TreeNodeID >& selection = ptr->GetSelection();
			if ( selection.Size() != 1 )
			{
				return;
			}

			const TreeNodeID& selectedNodeId = *selection.Begin();
			BugFlagsIssueNode* issueNode = ptr->GetNode<BugFlagsIssueNode>( selectedNodeId );

			ptr->EditIssue( issueNode );
		} );

		op.RegisterFunctor<void>( "openInBrowser", [ptr] ( interop::IContext& ctx )
		{
			const red::HashSet< TreeNodeID >& selection = ptr->GetSelection();
			if ( selection.Size() != 1 )
			{
				return;
			}

			const TreeNodeID& selectedNodeId = *selection.Begin();
			BugFlagsIssueNode* issueNode = ptr->GetNode<BugFlagsIssueNode>( selectedNodeId );

			ptr->OpenInBrowser( issueNode );
		} );

		op.RegisterFunctor<void>( "refreshIssues", [ ptr ]( interop::IContext& ctx )
		{
			ptr->FillTree();
		} );

	}

} // tools
