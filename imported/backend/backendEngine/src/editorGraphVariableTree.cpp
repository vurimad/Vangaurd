/**
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editorGraphVariableTree.h"
#include "editorGraphTypes.h"
#include "editorGraphEx.h"
#include "../../backendEngine/include/objectCollection.h"

namespace tools
{
	class VariableNodeBase : public utils::SimpleNode
	{
		DECLARE_INTEROP_DERIVED( VariableNodeBase, SimpleNode );
	public:
		VariableNodeBase( IEditorGraphEx* graph, VariableTree* tree )
			: m_graph( graph )
			, m_tree( tree )
		{
		}

	protected:
		VariableTree* m_tree;
		IEditorGraphEx* m_graph;
	};

	void VariableNodeBase::SetupInterop( VariableNodeBase* ptr, ::interop::utils::Object& op )
	{
	}

	///////////////////////////////////////////////////////////////////////////

	class VariableNode : public VariableNodeBase
	{
		DECLARE_INTEROP_DERIVED( VariableNode, VariableNodeBase );
	public:
		VariableNode( IEditorGraphEx* graph, VariableTree* tree, GraphVariableId id )
			: VariableNodeBase( graph, tree )
			, m_id( id )
		{
		}

		virtual String GetName() const override 
		{ 
			String name;
			m_graph->GetVariableName( m_id, name );

			return name;
		}
	
		virtual ISerializable* GetSerializable() const override
		{ 
			return m_graph->GetVariableData( m_id ).Get();
		}

	private:
		GraphVariableId m_id;
	};

	void VariableNode::SetupInterop( VariableNode* ptr, ::interop::utils::Object& op )
	{
		op.RegisterFunctor<void, String>( "rename", [ptr](::interop::IContext& ctx, const String& name) 
			{
				if ( ptr->m_graph->SetVariableName( ptr->m_id, name ) )
				{
					ptr->m_tree->InvalidateNodes_DEPRECATED(ptr->GetParentId(), { ptr->GetId() });
					ptr->m_tree->GetDataCollection()->Refresh( true );
				}
				else
				{
					ctx.ThrowException( "Failed to rename given variable" );
				}
			} );

		op.RegisterFunctor<void>( "remove", [ptr]( ::interop::IContext& ctx ) 
			{
				if ( ptr->m_graph->RemoveVariable( ptr->m_id ) )
				{
					ptr->m_tree->RemoveNode( ptr->GetId() );
					return;
				}

				ctx.ThrowException( "Failed to rename given variable" );
			} );
	}

	///////////////////////////////////////////////////////////////////////////

	class VariableCategoryNode : public VariableNodeBase
	{
		DECLARE_INTEROP_DERIVED( VariableCategoryNode, VariableNodeBase );
	public:
		VariableCategoryNode( IEditorGraphEx* graph, VariableTree* tree, const String& categoryName )
			: VariableNodeBase( graph, tree )
			, m_categoryName( categoryName )
		{
		}

		virtual String GetName() const override 
		{ 
			return m_categoryName; 
		}

		void CreateVariableNode( GraphVariableId id )
		{
			auto variable = red::CreateSharedPtr< VariableNode >( m_graph, m_tree, id );
			m_tree->AddNode( variable, *this );
		}

	private:		
		String m_categoryName;
	};
		
	void VariableCategoryNode::SetupInterop( VariableCategoryNode* ptr, ::interop::utils::Object& op )
	{
		op.RegisterFunctor<void, String>( "createVariable", [ptr]( ::interop::IContext& ctx, const String& name ) 
		{
			GraphVariableId id = ptr->m_graph->CreateVariable( ptr->m_categoryName );
			if ( id != InvalidGraphId )
			{
				ptr->m_graph->SetVariableName( id, name );
				ptr->CreateVariableNode( id );

				return;
			}
			ctx.ThrowException( "Failed to create variable" );										
		} );
	}

	///////////////////////////////////////////////////////////////////////////

	class VariableNodeRoot : public utils::SimpleNode
	{
		DECLARE_INTEROP_DERIVED( VariableNodeRoot, SimpleNode );
	public:
		VariableNodeRoot( IEditorGraphEx* graph, VariableTree* tree )
			: m_graph( graph )
			, m_tree( tree )
		{
		}
	
		virtual String GetName() const override 
		{ 
			return "Variables";
		}		

		void RecreateNodes()
		{
			m_categories.Clear();

			if ( !m_graph->IsSupportVariables() )
				return;

			red::DynArray<String> types;
			m_graph->ListVariableTypes( types );

			for ( const String& typeName : types )
			{
				VariableCategoryNode* node = GetNode( typeName );
				if ( !node )
				{
					auto newNode = red::CreateSharedPtr< VariableCategoryNode >( m_graph , m_tree, typeName );

					m_categories.Insert( typeName, newNode.Get() );
					m_tree->AddNode( newNode, *this );
				}
			}

			red::DynArray< GraphVariableId > variableIds;
			m_graph->GetVariables( variableIds );

			for ( GraphVariableId id : variableIds )
			{
				String type;
				m_graph->GetVariableType( id, type );
				VariableCategoryNode* category = GetNode( type );

				category->CreateVariableNode( id );
			}
		}

	private:

		VariableCategoryNode* GetNode( const String& variableType )
		{
			VariableCategoryNode* node;
			if ( m_categories.Find( variableType, node ) )
			{
				return node;
			}
			return nullptr;
		}

		VariableTree* m_tree;
		IEditorGraphEx* m_graph;

		red::HashMap< String, VariableCategoryNode* > m_categories;
	};	

	void VariableNodeRoot::SetupInterop( VariableNodeRoot* ptr, ::interop::utils::Object& op )
	{
	}

	///////////////////////////////////////////////////////////////////////////

	VariableTree::VariableTree( IEditActionHistory* editHistory, ObjectCollection* sharedSelection, IEditorGraphEx* graph )
		: m_graph( graph )
 		, m_dataCollection( sharedSelection )
	{
		m_root = red::CreateSharedPtr< VariableNodeRoot >( m_graph, this );
		m_root->RecreateNodes();

		SetRoot( m_root );
	}

	void VariableTree::SetupInterop( VariableTree* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "selection", [ptr]( interop::IContext& ctx )
		{
			return ptr->m_dataCollection->GetRootInterop();
		} );
	}

	void VariableTree::RefreshTree()
	{
		m_root->RecreateNodes();
	}

	void VariableTree::SetSelection( const TTreeNodeIDs& nodeIDs )
	{
		SimpleTree::SetSelection( nodeIDs );

		if ( nodeIDs.Size() != 1 )
		{
			m_dataCollection->Reset();
			return;
		}

		const utils::SimpleNode* treeNode = static_cast< const utils::SimpleNode* >( GetNode( nodeIDs[0] ) );
		if ( treeNode )
		{
			ISerializable* selectedObject = treeNode->GetSerializable();
			if ( selectedObject )
				m_dataCollection->SetObject( selectedObject, false, m_userContext);
			else
				m_dataCollection->Reset();
		}
	}	
}
