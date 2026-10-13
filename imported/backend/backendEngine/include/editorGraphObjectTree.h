/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "editorGraphNodeClassesDescriptor.h"
#include "../../../common/generalEditorBackend/include/treeUtils.h"

namespace tools
{
	struct ObjectTreeStructure;
	class GraphObjectTree;
	class IEditorGraphEx;
	class ITreeNodeFilterVisitor;

	class BACKEND_ENGINE_API GraphObjectTreeItem : public utils::NamedNode
	{
		DECLARE_INTEROP_DERIVED( GraphObjectTreeItem, NamedNode )

	public:
		GraphObjectTreeItem( const String& name, const String& data, GraphObjectTree* tree );

		const String& GetData() const { return m_data; }

		static const Uint32 ITEM_TYPE_ID = 1;
		virtual Uint32 GetDynamicType() const { return ITEM_TYPE_ID; }

		virtual THandle<TreeNodeData> CreateNodeDataStruct() const override;
		virtual void FillNodeDataStruct( const THandle<TreeNodeData>& nodeData ) const override;

	private:
		String m_data;
	};

	//----

	class BACKEND_ENGINE_API GraphObjectTreeGroup : public utils::NamedNode
	{
		DECLARE_INTEROP_DERIVED( GraphObjectTreeGroup, NamedNode )

		virtual Bool CanBeActivated() const override { return false; }

	public:
		GraphObjectTreeGroup( const String& name, GraphObjectTree* tree );
	};

	//----

	class BACKEND_ENGINE_API GraphObjectTree : public utils::SimpleTree
	{
		DECLARE_INTEROP_DERIVED( GraphObjectTree, SimpleTree )
		RED_BASE_CLASS( utils::SimpleTree );
	public:
		GraphObjectTree( const IEditorGraphEx* graph );

		const IEditorGraphEx* GetGraph() const;

		void GetStructure( ObjectTreeStructure& structure );

	private:
		const IEditorGraphEx* m_graph;

		red::UniquePtr< ITreeNodeFilterVisitor > m_connectionFilter;
		red::UniquePtr< ITreeNodeFilterVisitor > m_immediateNodeFilter;
	};

	//----

	class BACKEND_ENGINE_API GraphObjectTreeBuilder
	{
	public:
		GraphObjectTreeBuilder( GraphObjectTree* tree );

		// NOTE: when @path is empty the new item will be added under 'root'
		// @path should be constructed as "token1;token2;token3"
		// missing path group items are automatically added
		void AddItem( const String& name, const String& className, const String& path = String::EMPTY() );

		// NOTE: used to preserve backward compatibility with CNodeClassDescriptor interface, no need to use it in a custom impl
		void AddChildren( const THandle< CNodeClassDescriptor >& desc, const String& currentPath );

	private:
		ITreeNode* GetParent( const String& parentName );

		GraphObjectTree* m_tree;
		red::SharedPtr< GraphObjectTreeGroup > m_root;
	};
}