/**
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editorGraphObjectTree.h"
#include "editorGraphEx.h"
#include "../../../common/generalEditorBackend/include/graphObjectTreeItemData.h"

namespace tools
{
	GraphObjectTreeItem::GraphObjectTreeItem( const String& name, const String& data, GraphObjectTree* tree )
		: utils::NamedNode( name, tree )
		, m_data( data )
	{
	}

	void GraphObjectTreeItem::SetupInterop( GraphObjectTreeItem* ptr, ::interop::utils::Object& op )
	{
		op.RegisterValueRO<String>( "data", ptr->m_data );
	}

	THandle< TreeNodeData > GraphObjectTreeItem::CreateNodeDataStruct() const
	{
		return CreateHandle< GraphObjectTreeItemData >();
	}

	void GraphObjectTreeItem::FillNodeDataStruct( const THandle<TreeNodeData>& nodeData ) const
	{
		utils::NamedNode::FillNodeDataStruct( nodeData );

		THandle< GraphObjectTreeItemData > objectData = Cast< GraphObjectTreeItemData >( nodeData );

		const NamedNode* parent = static_cast< const NamedNode* >( m_treeFallbackPtr->GetNode( GetParentId() ) );
		if ( parent )
			objectData->m_categoryName = parent->GetName();

		objectData->m_data = m_data;
	}

	//----

	GraphObjectTreeGroup::GraphObjectTreeGroup( const String& name, GraphObjectTree* tree )
		: utils::NamedNode( name, tree )
	{
	}

	void GraphObjectTreeGroup::SetupInterop( GraphObjectTreeGroup* ptr, ::interop::utils::Object& op )
	{
	}

	//----

	GraphObjectTree::GraphObjectTree( const IEditorGraphEx* graph )
		: m_graph( graph )
	{
	}

	const IEditorGraphEx* GraphObjectTree::GetGraph() const
	{
		return m_graph;
	}

	void CollectItems( const ITreeNode* node, ObjectTreeStructure& structure )
	{
		for ( const auto& child : node->GetChildren() )
		{
			if ( child->GetDynamicType() == GraphObjectTreeItem::ITEM_TYPE_ID )
			{
				auto* item = static_cast< GraphObjectTreeItem* >( child.Get() );
				THandle< GraphObjectTreeItemData > itemData = Cast< GraphObjectTreeItemData >( item->CreateNodeDataStruct() );
				item->FillNodeDataStruct( itemData );

				structure.m_items.PushBack( std::move( itemData ) );
			}

			CollectItems( child.Get(), structure );
		}
	}

	void GraphObjectTree::GetStructure( ObjectTreeStructure& structure )
	{
		const ITreeNode* root = GetNode( GetRootID() );
		CollectItems( root, structure );
	}

	//----

	class GraphClassTreeConnectionFilter : public ITreeNodeFilterVisitor
	{
	public:
		GraphClassTreeConnectionFilter( const IEditorGraphEx* graph, GraphNodeId outputNodeId, GraphSocketId outputSocketId )
			: m_graph( graph )
			, m_outputNodeId( outputNodeId )
			, m_outputSocketId( outputSocketId )
		{
		}

		virtual Bool PassesCriteria( const ITreeNode* node ) override
		{
			if ( node->GetDynamicType() == GraphObjectTreeItem::ITEM_TYPE_ID )
			{
				const GraphObjectTreeItem* graphItem = static_cast<const GraphObjectTreeItem*>( node );
				return m_graph->CanCreateConnection( m_outputNodeId, m_outputSocketId, graphItem->GetData() );
			}
			return false;
		}

	private:
		const IEditorGraphEx* m_graph;
		GraphNodeId m_outputNodeId;
		GraphSocketId m_outputSocketId;
	};

	class GraphClassTreeImmediateNodeFilter : public ITreeNodeFilterVisitor
	{
	public:
		GraphClassTreeImmediateNodeFilter( const IEditorGraphEx* graph, const ConnectionCreationInfo& info )
			: m_graph( graph )
			, m_info( info )
		{
		}

		virtual Bool PassesCriteria( const ITreeNode* node ) override
		{
			if ( node->GetDynamicType() == GraphObjectTreeItem::ITEM_TYPE_ID )
			{
				const GraphObjectTreeItem* graphItem = static_cast<const GraphObjectTreeItem*>( node );
				return m_graph->CanCreateIntermediateNode( m_info, graphItem->GetData() );
			}
			return false;
		}

	private:
		const IEditorGraphEx* m_graph;
		ConnectionCreationInfo m_info;
	};

	void GraphObjectTree::SetupInterop( GraphObjectTree* ptr, ::interop::utils::Object& op )
	{
		op.RegisterFunctor<void, GraphNodeId, GraphSocketId>( "filterByCanConnect", 
			[ptr]( interop::IContext& ctx, GraphNodeId outputNodeId, GraphSocketId outputSocketId )
		{
			if( ptr->m_connectionFilter )
			{
				ptr->DetachFilter( *ptr->m_connectionFilter );
			}
			ptr->m_connectionFilter = red::CreateUniquePtr< GraphClassTreeConnectionFilter >( ptr->GetGraph(), outputNodeId, outputSocketId );
			ptr->AttachFilter( *ptr->m_connectionFilter, true );
		} );

		op.RegisterFunctor<void, ConnectionCreationInfo >( "filterByImmediateNode",
			[ptr]( interop::IContext& ctx, const ConnectionCreationInfo& info )
		{
			if( ptr->m_immediateNodeFilter )
			{
				ptr->DetachFilter( *ptr->m_immediateNodeFilter );
			}
			ptr->m_immediateNodeFilter = red::CreateUniquePtr< GraphClassTreeImmediateNodeFilter  >( ptr->GetGraph(), info );
			ptr->AttachFilter( *ptr->m_immediateNodeFilter, true );
		} );

		op.RegisterFunctor<void>( "clearFilters",
			[ptr]( interop::IContext& ctx )
		{
			ptr->ClearFilter();
			ptr->m_connectionFilter = nullptr;
			ptr->m_immediateNodeFilter = nullptr;
		} );
	}

	//----

	GraphObjectTreeBuilder::GraphObjectTreeBuilder( GraphObjectTree* tree )
		: m_tree( tree )
	{
		m_root = red::CreateSharedPtr< GraphObjectTreeGroup >( "root", tree );
		tree->SetRoot( m_root );
	}

	void GraphObjectTreeBuilder::AddChildren( const THandle< CNodeClassDescriptor >& desc, const String& currentPath )
	{
		for ( auto& childDesc : desc->GetChildren() )
		{
			AddItem( childDesc->GetName(), childDesc->GetClassName().AsChar(), currentPath );
			AddChildren( childDesc, String::Printf( "%hs;%hs", currentPath.AsChar(), childDesc->GetName().AsChar() ) );
		}
	}

	ITreeNode* GraphObjectTreeBuilder::GetParent( const String& path )
	{
		red::DynArray< String > pathTokens{ red::PoolFrame() };
		path.GetTokens( ';', false, pathTokens );

		auto* parent = static_cast< utils::NamedNode* >( m_root.Get() );
		for ( const String& token : pathTokens )
		{
			utils::NamedNode* newParent = parent;
			for ( const auto& child : parent->GetChildren() )
			{
				auto* namedChild = static_cast< utils::NamedNode* >( child.Get() );
				if ( namedChild->GetName() == token )
				{
					newParent = namedChild;
					break;
				}
			}
				
			if ( parent == newParent )
			{
				auto newParentShared = red::CreateSharedPtr< GraphObjectTreeGroup >( token, m_tree );
				newParent = newParentShared.Get();
				m_tree->AddNode( newParentShared, *parent );
			}

			parent = newParent;
		}

		return parent;
	}

	void GraphObjectTreeBuilder::AddItem( const String& name, const String& className, const String& path )
	{
		red::SharedPtr< GraphObjectTreeItem > item = red::CreateSharedPtr< GraphObjectTreeItem >( name, className, m_tree );
		ITreeNode* parent = GetParent( path );

		m_tree->AddNode( item, *parent );
	}
}
