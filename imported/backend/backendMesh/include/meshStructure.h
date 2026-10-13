#pragma once

#include "../../../common/generalEditorBackend/include/treeUtils.h"

#include "../../../common/resourceMesh/include/meshDataViewRO.h"

namespace tools
{
	class MeshResourceEditor;
	class StructureTree;
	class LodsNode;	
	class IMeshStructureNodeVisualization;
	class ObjectCollection;
	class IEditActionHistory;

	struct MeshVisualizationRenderContext
	{
		MeshVisualizationRenderContext(rend::IDebugDrawer& dd)
			: m_drawer(&dd)
			, m_texturesCount( 0 )
		{}

		rend::IDebugDrawer* m_drawer;
		Uint32 m_texturesCount;
	};

	class IMeshStructureNodeVisualization
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IMeshStructureNodeVisualization() {};
		virtual CName GetType() const = 0;
		virtual Int32 GetRenderOrder() { return 0; }
		virtual void RenderDebug(MeshVisualizationRenderContext& context) = 0;
	};

	using MeshStructureNodeVisualizationPtr = red::UniquePtr< IMeshStructureNodeVisualization >;

	class IMeshVisualizationContainer
	{
	public:
		IMeshVisualizationContainer() {};
		virtual ~IMeshVisualizationContainer() {};

		virtual void RemoveAllVisualizations() = 0;
		virtual void AttachVisualization( MeshStructureNodeVisualizationPtr&& v) = 0;

		virtual void SetMeshVisibility(const Bool isVisible) = 0;
		virtual Bool SetForcedLOD(Int32 lodLevel) = 0;
		virtual const Int32 GetForcedLOD() = 0;
	};

	/// basic node in the mesh structure
	class BACKEND_MESH_API IMeshStructureNode : public utils::SimpleNode
	{
		DECLARE_INTEROP(IMeshStructureNode);

	public:
		IMeshStructureNode(StructureTree& tree);
		virtual ~IMeshStructureNode();

		RED_FORCE_INLINE StructureTree& GetTree() { return *m_tree; }
		RED_FORCE_INLINE const StructureTree& GetTree() const { return *m_tree; }

		/// get the editable data at this node
		virtual THandle< ISerializable > GetEditableObject() = 0;

		/// create debug visualization at this node
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const = 0;
		virtual Bool OnDragAndDrop( ITreeNode* target ) override { return false; };

		/// create nodes
		virtual void CreateNodes() = 0;

		/// recreate nodes at given level
		/// 0 = this
		/// 1 = parent
		/// etc
		void RecreateNodes(Int32 level = 0);

		/// refresh this node test
		void RefreshNodeCaption();
		void RefreshParentNodeCaption();
	
	private:
		StructureTree*	m_tree;
	};

	struct MeshMaterialDesc
	{
		CName m_name;
		Uint32 m_index;

		RED_FORCE_INLINE MeshMaterialDesc(const CName& name, Uint32 ind)
			: m_name(name)
			, m_index(ind)
		{}
	};

	/// mesh structure tree, provides automatic data exposition and debug visualization
	class BACKEND_MESH_API StructureTree : public utils::SimpleTree
	{
	public:
		StructureTree( MeshResourceEditor* editor, ObjectCollection* propertyGrid, const std::function<void(void)>& meshChangedCallback );
		~StructureTree();

		RED_FORCE_INLINE const THandle< CMesh >& GetMesh() const { return m_mesh; }

		RED_FORCE_INLINE const mesh::DataViewRO& GetMeshView() const { return *m_view; }

		RED_FORCE_INLINE const MeshResourceEditor* GetMeshResourceEditor() const // TODO: AD probably would be good idea to introduce some shared data and use it instead of whole editor
		{
			return m_editorFallback;
		}

		virtual void CreateRoot();

		IEditActionHistory& GetAccessHistory() const;

		void SetMesh(const THandle< CMesh >& mesh);

		void RefreshTree();

		void RefreshVisualization();

		void RefreshMeshPreview();

		void AddNode( const red::SharedPtr< ITreeNode >& node, ITreeNode& parent, Bool shouldCreateNodes = true );

		void AttachVisualizationContainer(IMeshVisualizationContainer* visContainer);

		void DetachVisualizationContainer(IMeshVisualizationContainer* visContainer);

		virtual Bool DragAndDropNode_DEPRECATED( TreeNodeID sourceNodeID, TreeNodeID targetNodeID ) override;

		void SelectNodeFromPreviewSelection( const TTreeNodeIDs& nodeIDs );
	protected:
		ObjectCollection* m_objectEditor;
		MeshResourceEditor* m_editorFallback;

		red::SharedPtr< IMeshStructureNode > m_rootNode;
	private:
		THandle< CMesh > m_mesh;
		red::UniquePtr< mesh::DataViewRO > m_view;

		red::DynArray< MeshMaterialDesc > m_layeredMaterials;
		red::DynArray< MeshMaterialDesc > m_nonLayeredMaterials;

		red::DynArray< IMeshVisualizationContainer* > m_visualizationContainers;

		TTreeNodeIDs m_visualizationSelection;

		std::function<void( void )> m_meshChangedCallback;


		virtual void SetSelection( const TTreeNodeIDs& nodeIDs ) override final;
	};

}