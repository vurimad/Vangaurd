#include "build.h"
#include "meshStructure.h"
#include "meshStructureNodes.h"
#include "backendMeshEditor.h"
#include "backendMeshPanelSharedData.h"
#include "physicsObjectActionsHelper.h"
#include "physicsBase.h"

#include "../../backendEngine/include/objectSerializablePersistentCollection.h"
#include "../../backendEngine/include/objectHandler.h"
#include "../../backendEngine/include/editActionHistory.h"
#include "../../backendEngine/include/selectionState.h"
#include "physicsObjectResolver.h"

namespace tools
{
	IMeshStructureNode::IMeshStructureNode(StructureTree& tree)
		: m_tree(&tree)
	{}

	IMeshStructureNode::~IMeshStructureNode()
	{}

	void IMeshStructureNode::RecreateNodes(Int32 level/*= 0*/)
	{
		auto* node = this;
		auto* tree = m_tree;

		while (level > 0 && node->GetParentId() != InvalidTreeID)
		{
			level -= 1;

			auto* parent = tree->GetNode(node->GetParentId());
			if (!parent)
				break;

			node = (IMeshStructureNode*) parent;
			tree = node->m_tree;
		}

		if (node)
		{
			auto children = node->GetChildren();

			for (auto it : children)
				tree->RemoveNode(it->GetId());

			node->CreateNodes();
		}

		tree->RefreshVisualization();
	}

	void IMeshStructureNode::RefreshNodeCaption()
	{
		TreeNodeID parentId = GetParentId();

		TTreeNodeIDs nodesToInvalide;
		nodesToInvalide.PushBack(GetId());
		GetTree().InvalidateNodes_DEPRECATED(parentId, nodesToInvalide);
	}

	void IMeshStructureNode::RefreshParentNodeCaption()
	{
		TreeNodeID parentId = GetParentId();
		ITreeNode* parent = GetTree().GetNode( parentId );

		if ( parent )
		{
			IMeshStructureNode* parentMeshNode = static_cast< IMeshStructureNode* >( parent );
			parentMeshNode->RefreshNodeCaption();
		}
	}

	void IMeshStructureNode::SetupInterop(IMeshStructureNode* ptr, ::interop::utils::Object& op)
	{
		utils::SimpleNode::SetupInterop(ptr, op);
	}

	//-------------------------

	StructureTree::StructureTree( MeshResourceEditor* editor, ObjectCollection* propertyGrid, const std::function<void( void )>& meshChangedCallback )
		: m_editorFallback( editor )
		, m_mesh(editor->GetMesh())
		, m_view( red::CreateUniquePtr< mesh::DataViewRO >( *editor->GetMesh() ))
		, m_objectEditor( propertyGrid )
		, m_layeredMaterials( red::PoolBackend() )
		, m_nonLayeredMaterials( red::PoolBackend() )
		, m_visualizationContainers( red::PoolBackend() )
		, m_meshChangedCallback( meshChangedCallback )
	{
	}

	StructureTree::~StructureTree()
	{}

	void StructureTree::CreateRoot()
	{
		m_rootNode = red::CreateSharedPtr< MeshNode_Root >( *this );
		SetRoot( m_rootNode );
		m_rootNode->CreateNodes();
	}

	IEditActionHistory& StructureTree::GetAccessHistory() const
	{
		return m_editorFallback->GetAccessHistory();
	}

	void StructureTree::SetMesh(const THandle< CMesh >& mesh)
	{
		m_mesh = mesh;
		m_view = red::CreateUniquePtr< mesh::DataViewRO >(*mesh);
		m_rootNode->RecreateNodes();
	}

	void StructureTree::RefreshTree()
	{
		m_visualizationSelection.Clear();
		m_rootNode->RecreateNodes();
	}

	void StructureTree::AttachVisualizationContainer(IMeshVisualizationContainer* visContainer)
	{
		m_visualizationContainers.PushBack(visContainer);
		RefreshVisualization();
	}

	void StructureTree::DetachVisualizationContainer(IMeshVisualizationContainer* visContainer)
	{
		if (m_visualizationContainers.Exist(visContainer))
		{
			visContainer->RemoveAllVisualizations();
			m_visualizationContainers.Remove(visContainer);
		}
	}

	void StructureTree::RefreshVisualization()
	{
		for (auto* vis : m_visualizationContainers)
		{
			vis->RemoveAllVisualizations();
			//vis->SetForcedLOD(-1);
			vis->SetMeshVisibility(true);

			for (auto id : m_visualizationSelection)
			{
				auto* node = (IMeshStructureNode*)GetNode(id);
				if (node != nullptr)
					node->SetupVisualization(vis);
			}
		}	
	}

	void StructureTree::RefreshMeshPreview()
	{
		if ( m_meshChangedCallback )
			m_meshChangedCallback();
	}

	void StructureTree::AddNode( const red::SharedPtr< ITreeNode >& node, ITreeNode& parent, Bool shouldCreateNodes )
	{
		utils::SimpleTree::AddNode( node, parent );
		if ( shouldCreateNodes )
		{
			static_cast<IMeshStructureNode&>( *node ).CreateNodes();
		}
	}

	void StructureTree::SelectNodeFromPreviewSelection( const TTreeNodeIDs& nodeIDs )
	{
		SimpleTree::SetSelection( nodeIDs );

		m_visualizationSelection = nodeIDs;

		if( m_objectEditor )
		{
			red::DynArray< THandle< ISerializable > > dataObjects = { red::PoolBackend() };
			red::DynArray< EditorObjectID > selectedObjectIDs = { red::PoolBackend() };
			PhysicsObjectResolver* resolver = nullptr;
			for( auto id : m_visualizationSelection )
			{
				auto* node = (IMeshStructureNode*)GetNode( id );
				if( node != nullptr )
				{
					const auto& data = node->GetEditableObject();
					if( data )
					{
						if( THandle< physics::PhysicsBase > physObject = Cast< physics::PhysicsBase >( data ) )
						{
							selectedObjectIDs.PushBack( data->GetEditorObjectID() );
						}
						dataObjects.PushBack( data );
					}
				}
			}

			m_objectEditor->SetObjects( dataObjects );
		}
		RefreshVisualization();
	}

	void StructureTree::SetSelection( const TTreeNodeIDs& nodeIDs )
	{
		SimpleTree::SetSelection( nodeIDs );

		m_visualizationSelection = nodeIDs;

		if( m_objectEditor )
		{
			red::DynArray< THandle< ISerializable > > dataObjects;
			red::DynArray< EditorObjectID > selectedObjectIDs;
			PhysicsObjectResolver* resolver = nullptr;
			for( auto id : m_visualizationSelection )
			{
				auto* node = (IMeshStructureNode*)GetNode( id );
				if( node != nullptr )
				{
					auto data = node->GetEditableObject();
					if( data )
					{
						if( THandle< physics::PhysicsBase > physObject = Cast< physics::PhysicsBase >( data ) )
						{
							selectedObjectIDs.PushBack( data->GetEditorObjectID() );
						}
						dataObjects.PushBack( data );
					}
				}
			}			

			m_objectEditor->SetObjects( dataObjects );
			if( selectedObjectIDs.Size() > 0 )
			{
				if( const MeshPanelSharedData* previewSharedDataPtr = m_editorFallback->GetMeshPanelSharedData() )
				{
					const ObjectHandler& objectHandler = previewSharedDataPtr->GetObjectHandler();
					if( auto resolver = objectHandler.GetUnifiedResolver( ObjectSerializablePersistentCollection::CaptureObjects( dataObjects ) ) )
					{
						if( resolver->GetClass()->IsA<PhysicsObjectResolver>() )
						{
							PhysicsObjectResolver* physicsResolver = Cast<PhysicsObjectResolver>( resolver );
							IEditActionHistory& actionHistory = m_editorFallback->GetAccessHistory();
							const SelectionStatePtr& selection = previewSharedDataPtr->GetSelection();
							if( selection )
							{
								actionHistory.Execute( red::CreateUniquePtr< tools::Action_SelectObject >( physicsResolver, &previewSharedDataPtr->GetSelectionHandler(), selectedObjectIDs ) );
							}
						}										
					}
					else 
					{
						dataObjects.Clear();
						selectedObjectIDs.Clear();
						m_objectEditor->SetObjects( dataObjects );
					}
				}
			}
		}

		RefreshVisualization();
	}

	Bool StructureTree::DragAndDropNode_DEPRECATED( TreeNodeID sourceNodeID, TreeNodeID targetNodeID )
	{
		auto targetNode = GetNode( targetNodeID );
		auto sourceNode = GetNode( sourceNodeID );
		if( targetNode && sourceNode )
		{
			if( sourceNode->OnDragAndDrop( targetNode ) )
			{
				RefreshTree();
			}
			return true;
		}
		return false;
	}
}