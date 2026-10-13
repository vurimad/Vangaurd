#pragma once

#include "../../backendEngine/include/interopList.h"
#include "../../common/issueReporter/include/jiraDataStruct.h"
#include "../../common/issueReporter/include/jiraService.h"
#include "../../common/generalEditorBackend/include/treeUtils.h"

namespace world
{
	class BugFlagsSystem;
}

namespace tools
{
	class IResourceEditor;

	class BugFlagsIssueNode : public utils::NamedNode
	{
		RTTI_DECLARE_TYPE( BugFlagsIssueNode );
		RED_BASE_CLASS( utils::NamedNode );

	public:
		BugFlagsIssueNode();
		BugFlagsIssueNode( const JiraIssue& data, utils::SimpleTree* parentTree );

		virtual THandle< TreeNodeData > CreateNodeDataStruct() const;
		virtual void FillNodeDataStruct( const THandle< TreeNodeData >& nodeData ) const;

		virtual void SetNodeDataStruct( const THandle<TreeNodeData>& data ) override;

		virtual String ToString() const;

		const JiraIssue& GetJiraIssue() const { return m_data; }
		void SetJiraIssue( const JiraIssue& value );

		const Color& GetColor() const { return m_color; }

		const Vector3& GetFlagPosition() const { return m_flagPosition; }
		void SetFlagPosition( const Vector3& value ) { m_flagPosition = value; }

	private:
		void OnDataChange();

		JiraIssue m_data{};

		Vector3 m_flagPosition{};
		Color m_color{};
	};

	class BugFlagsTree : public utils::SimpleTree
	{
		DECLARE_INTEROP_DERIVED( BugFlagsTree, SimpleTree );

	public:
		BugFlagsTree( const res::ResourcePath& resourcePath );

		Event< BugFlagsIssueNode* > EditIssue;
		Event< BugFlagsIssueNode* > OpenInBrowser;

		virtual void FilterNodes( const String& filterString ) override;
		virtual void FilterNodes( const red::HashMap<String, String>& filtersMap ) override;

		void SetOAuthToken( const oauth::AuthenticationToken &token );

	protected:
		virtual const TreeNodeID AllocateNodeId( const ITreeNode& node ) override;

	private:
		void FillTree( const String& jql = String::EMPTY() );

		oauth::AuthenticationToken m_jiraOauth;
		res::ResourcePath m_resourcePath;
	};

} // tools