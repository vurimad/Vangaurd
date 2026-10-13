/*
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/backendData.h"
#include "../../../common/generalEditorBackend/include/treeUtils.h"

namespace tools
{
	class VariableNodeRoot;
	class IEditorGraphEx;
	class ObjectCollection;
	class IEditActionHistory;

	class VariableTree : public utils::SimpleTree
	{
		DECLARE_INTEROP_DERIVED( VariableTree, SimpleTree );
	public:
		VariableTree( IEditActionHistory* editHistory, ObjectCollection* sharedSelection, IEditorGraphEx* graph );

		virtual void SetSelection(const TTreeNodeIDs& nodeIDs) override;
		void RefreshTree();

		void SetUserContext(THandle<ISerializable> userContext) { m_userContext = userContext; }
		ObjectCollection* GetDataCollection() { return m_dataCollection; }

	private:
		TTreeNodeIDs m_selectedIDs;

		red::SharedPtr< VariableNodeRoot > m_root;
		IEditorGraphEx* m_graph;

		ObjectCollection* m_dataCollection;
		THandle<ISerializable> m_userContext;
	};
} // namespace tools
