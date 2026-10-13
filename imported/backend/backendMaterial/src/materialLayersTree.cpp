/**
* Copyright (c) 2017-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "materialLayersTree.h"

#include "materialLayerNode.h"
#include "materialController.h"
#include "../../backendEngine/include/objectCollection.h"

namespace tools
{
	void MaterialLayersTree::SetupInterop( MaterialLayersTree* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "selection", [ptr]( interop::IContext& ctx )
		{
			return ptr->m_selection->GetRootInterop();
		} );
	}

	MaterialLayersTree::MaterialLayersTree( LayerLibraryMaterialController* controller, tools::ResourceBackend* backend, const String& rootDirectory, IEditActionHistory* actionHistory )
		: m_controller( controller )
		, m_selectionRefresh( true )
	{
		m_dataProvider = red::CreateSharedPtr< MaterialLibraryDataProvider >( Cast< CMaterialLayerLibrary >( controller->GetMaterial() ), backend, rootDirectory );
		m_dataProvider->AttachListener( this );
		m_selection = RED_NEW( ObjectCollection )( *actionHistory );
		m_root = red::CreateSharedPtr< MaterialCategoryNode >( this, "Library", rootDirectory );
		SetRoot( m_root );
		Reset();
	}

	MaterialLayersTree::~MaterialLayersTree()
	{
		m_dataProvider->DetachListener( this );
	}

	void MaterialLayersTree::SetSelection( const TTreeNodeIDs& nodeIDs )
	{
		SimpleTree::SetSelection( nodeIDs );

		if ( nodeIDs.Size() != 1 )
		{
			m_selection->Reset();
			return;
		}

		const MaterialBaseNode* treeNode = static_cast<const MaterialBaseNode*>(GetNode( nodeIDs[0] ));
		if ( treeNode )
		{
			auto layerIter = m_layerNodes.Find( treeNode->GetName() );
			if ( layerIter != m_layerNodes.End() )
			{
				auto layerDesc = layerIter.Value()->GetDesc();
				ISerializable* selectedObject = layerDesc->GetResource().Get();
				// TODO unload unmodified resources
				if ( selectedObject )
				{
					m_selection->SetObject( selectedObject, layerIter.Value()->GetState() == NodeState::Readonly );
					m_controller->OnLayerSelected( layerDesc );
				}
				else
				{
					m_selection->Reset();
				}
			}
		}
	}

	void MaterialLayersTree::SelectNodes( const MaterialBaseNode* treeNode )
	{
		TTreeNodeIDs nodeIDs;
		nodeIDs.PushBack( treeNode->GetId() );
		SimpleTree::SetSelection( nodeIDs );

		if ( treeNode )
		{
			auto layerIter = m_layerNodes.Find( treeNode->GetName() );
			if ( layerIter != m_layerNodes.End() )
			{
				auto layerDesc = layerIter.Value()->GetDesc();
				ISerializable* selectedObject = layerDesc->GetResource().Get();
				// TODO unload unmodified resources
				if ( selectedObject )
				{
					m_selection->SetObject( selectedObject, layerIter.Value()->GetState() == NodeState::Readonly );
					m_controller->OnLayerSelected( layerDesc );
				}
				else
				{
					m_selection->Reset();
				}
			}
		}
	}

	void MaterialLayersTree::Reset()
	{
		m_selectionRefresh = false;
		Clear();
		m_dataProvider->Rescan();
		m_selectionRefresh = true;
	}

	Bool MaterialLayersTree::Save()
	{
		return m_dataProvider->Save();
	}

	void MaterialLayersTree::AddLayer( const String& name )
	{
		m_dataProvider->CreateLayer( name );
	}

	void MaterialLayersTree::RemoveLayer( MaterialInstanceNode* node )
	{
		//m_dataProvider->RemoveLayer( node->GetName() );

		RemoveNode( node->GetId() );
	}

	void MaterialLayersTree::OnReset()
	{
		Reset();
	}

	void MaterialLayersTree::OnCreateNode( MaterialLayerDescPtr& layerDesc )
	{
		auto catergories = layerDesc->GetTags();
		for ( auto categoryName : catergories )
		{
			auto categoryNode = GetCategoryNode( categoryName );
			CreateLayerNode( layerDesc, categoryNode );
		}
	}

	void MaterialLayersTree::OnRemoveNode( const String& name )
	{
		auto& nodeIter = m_layerNodes.Find( name );
		if ( nodeIter != m_layerNodes.End() )
		{
			RemoveNode( nodeIter.Value()->GetId() );
		}
	}

	void MaterialLayersTree::CreateLayerNode( MaterialLayerDescPtr& layerDesc, MaterialCategoryNode* categoryNode )
	{
		auto layerNode = red::CreateSharedPtr< MaterialInstanceNode >( this, layerDesc->GetName(), layerDesc->GetPath() );
		m_layerNodes[layerDesc->GetName()] = layerNode.Get();
		AddNode( layerNode, *categoryNode );
	}

	MaterialCategoryNode* MaterialLayersTree::GetCategoryNode( const String& name )
	{
		auto categoryIter = m_categoryNodes.Find( name );
		if ( categoryIter == m_categoryNodes.End() )
		{
			return CreateCategoryNode( name );
		}
		else
		{
			return categoryIter.Value();
		}
	}

	MaterialCategoryNode* MaterialLayersTree::CreateCategoryNode( const String& name )
	{
		auto categoryNode = red::CreateSharedPtr< MaterialCategoryNode >( this, name, "" );
		m_categoryNodes[name] = categoryNode.Get();
		AddNode( categoryNode, *m_root );
		return categoryNode.Get();
	}

}