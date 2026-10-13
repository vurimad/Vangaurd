#pragma once

#include "meshStructure.h"
#include "../../../common/resourceMesh/include/meshPacker.h"

class ITexture;

namespace physics
{
	class EditorActorCOM;
	class SystemResource;
	class SystemJoint;
	class ICollider;
	class PhysicsEditorScene;
	class PhysicsActor;
	class PhysicsShape;
	class PhysicsJoint;
	class PhysicsMesh;
	class PhysicsBase;
	struct SystemBody;
	using PhysicsActorPtr = THandle< PhysicsActor >;
	using PhysicsShapePtr = THandle< PhysicsShape >;
	using PhysicsJointPtr = THandle< PhysicsJoint >;
	using PhysicsMeshPtr = THandle< PhysicsMesh >;
}
namespace mesh
{
	class MeshParamWorkspotOffsets;
	class MeshParamImportedSnapPoint;
	class MeshParamOccluderData;
	class MeshParameter;
}

namespace tools
{
	class PhysicsObjectResolver;

	//-------------------------------------------------------------------------

	class MeshNode_Root : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_Root, IMeshStructureNode);

	public:
		MeshNode_Root(StructureTree& tree);
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final { return "Mesh"; }
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final {}
	};

	//-------------------------------------------------------------------------

	class MeshNode_Bounds : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_Bounds, IMeshStructureNode);

	public:
		MeshNode_Bounds(StructureTree& tree, const Box& bounds);
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final { return "Bounds"; }
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const override final;

	private:
		Box m_box;
	};

	//-------------------------------------------------------------------------

	class MeshNode_Lods : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_Lods, IMeshStructureNode);

	public:
		MeshNode_Lods(StructureTree& parentTree);
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final { return "LODs"; }
		virtual THandle< ISerializable > GetEditableObject()  override final { return nullptr; }
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const  override final {}
	};

	//-------------------------------------------------------------------------

	class MeshNode_Materials : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_Materials, IMeshStructureNode);

	public:
		MeshNode_Materials(StructureTree& parentTree);
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final { return "Materials"; }
		virtual THandle< ISerializable > GetEditableObject()  override final { return nullptr; }
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const  override final {}
	};

	//-------------------------------------------------------------------------

	class MeshNode_Appearances : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_Appearances, IMeshStructureNode);

	public:
		MeshNode_Appearances( StructureTree& parentTree );
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final { return "Appearances"; }
		virtual THandle< ISerializable > GetEditableObject()  override final { return nullptr; }
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const  override final {}
	};

	//-------------------------------------------------------------------------

	class MeshNode_Appearance : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_Appearance, IMeshStructureNode );

	public:
		MeshNode_Appearance( StructureTree& parentTree, const Uint32 index );
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject()  override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const  override final {}

	private:
		Uint32 m_index;
	};

	//-------------------------------------------------------------------------

	class MeshNode_AppearanceChunk : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_AppearanceChunk, SimpleNode );

		friend class MeshChunkSetup;

	public:
		MeshNode_AppearanceChunk( StructureTree& parentTree, const Uint32 index, const Uint32 appearanceIndex );
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final { return String::Printf( "Chunk%u", m_index ); }
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;

		Uint32 GetChunkIndex() const { return m_index; }

	private:
		void OnMaterialChanged();

		Uint32 m_index;
		Uint32 m_appearanceIndex;
		red::DynArray< IMeshStructureNode* > m_materialNodes;
	};

	//-------------------------------------------------------------------------

	class MeshNode_Bones : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_Bones, IMeshStructureNode);

	public:
		MeshNode_Bones(StructureTree& parentTree);
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final { return "Bones"; }
		virtual THandle< ISerializable > GetEditableObject()  override final { return nullptr; }
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const  override final {}
	};

	//-------------------------------------------------------------------------

	class MeshNode_LodLevel : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_LodLevel, IMeshStructureNode);

	public:
		MeshNode_LodLevel(StructureTree& parentTree, const Uint32 index, Bool lod0Removed);
		virtual void CreateNodes() override final;
		virtual String GetName() const override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const override final;

	private:
		Uint32 m_index;
		Bool m_lod0Removed = false;
	};

	//-------------------------------------------------------------------------

	class MeshNode_ChunkSkinning : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_ChunkSkinning, IMeshStructureNode);

	public:
		MeshNode_ChunkSkinning(StructureTree& parentTree, const Uint32 index, const Uint32 lodIndex);
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final { return "Skinning"; }
		virtual THandle< ISerializable > GetEditableObject()  override final { return nullptr; }
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const  override final;

	private:
		Uint32 m_chunkIndex;
		Uint32 m_lodIndex;
	};

	//-------------------------------------------------------------------------

	class MeshNode_ChunkStreams : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_ChunkStreams, IMeshStructureNode);

	public:
		MeshNode_ChunkStreams(StructureTree& parentTree, const Uint32 index, const Uint32 lodIndex);
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final { return "Streams"; }
		virtual THandle< ISerializable > GetEditableObject()  override final { return nullptr; }
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const  override final;

	private:
		Uint32 m_chunkIndex;
		Uint32 m_lodIndex;
	};

	//-------------------------------------------------------------------------

	class MeshNode_ChunkStream : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_ChunkStream, IMeshStructureNode);

	public:
		MeshNode_ChunkStream(StructureTree& parentTree, const Uint32 index, const Uint32 lodIndex, EMeshStreamType stream);
		virtual void CreateNodes() override final {};
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const override final;

	private:
		EMeshStreamType m_stream; 
		Uint32 m_chunkIndex;
		Uint32 m_lodIndex;
	};

	//-------------------------------------------------------------------------

	class MeshNode_ChunkStream_GarmentCaps : public IMeshStructureNode                      // We need special handling of this, this is actually a "virtual" stream, part of garment flags stream.
	{
		DECLARE_INTEROP_DERIVED( MeshNode_ChunkStream_GarmentCaps, IMeshStructureNode );

	public:
		MeshNode_ChunkStream_GarmentCaps( StructureTree& parentTree, const Uint32 index, const Uint32 lodIndex );
		virtual void CreateNodes() override final {};
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;

	private:
		Uint32 m_chunkIndex;
		Uint32 m_lodIndex;
	};

	//-------------------------------------------------------------------------

	class MeshNode_Chunk : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_Chunk, SimpleNode);

	public:
		MeshNode_Chunk(StructureTree& parentTree, const Uint32 index, const Uint32 lodIndex);
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const override final;

		Uint32 GetChunkIndex() const { return m_index; }
	private:
		Uint32 m_index;
		Uint32 m_lodIndex;

		friend class MeshChunkSetup;
	};

	//-------------------------------------------------------------------------

	class MeshNode_MaterialReference : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_MaterialReference, SimpleNode);

	public:
		MeshNode_MaterialReference(StructureTree& parentTree, const Uint32 materialInd, const Int32 chunkIndex, const Int32 appearanceIndex );
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;

	private:
		Int32 m_chunkIndex;
		Int32 m_appearanceIndex;
		Uint32 m_materialInd;            ///< When both m_chunkIndex & m_appearanceIndex are -1, this means material index is index inside whole mesh.

		CName GetMeshMaterialName() const;
		Uint32 GetMeshMaterialIndex() const;
	};

	//-------------------------------------------------------------------------

	class MeshNode_TextureReference : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_TextureReference, SimpleNode);

	public:
		MeshNode_TextureReference(StructureTree& parentTree, THandle< ITexture > texture, const String& paramName);
		virtual void CreateNodes() override final {};
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const override final;

	private:
		THandle< ITexture > m_texture;
		String m_paramName;
	};

	//-------------------------------------------------------------------------

	class MeshNode_BoneReference : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_BoneReference, SimpleNode);

	public:
		MeshNode_BoneReference(StructureTree& parentTree, const Uint32 boneIndex);
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;

	private:
		Uint32 m_boneIndex;
	};

	//-------------------------------------------------------------------------

	class MeshNode_BoneInfluences : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_BoneInfluences, SimpleNode);

	public:
		MeshNode_BoneInfluences(StructureTree& parentTree, const Int32 chunkIndex, const Uint32 boneIndex, const Uint32 vertexCount, const Int32 lodIndex);
		virtual void CreateNodes() override final {};
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const override final;

	private:
		Int32 m_chunkIndex;
		Uint32 m_boneIndex;
		Int32 m_lodIndex;

		Uint32 m_vertexCount;
	};

	//-------------------------------------------------------------------------

	class MeshNode_MeshParameters : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_MeshParameters, SimpleNode );

	public:
		MeshNode_MeshParameters( StructureTree& parentTree  );
		virtual void CreateNodes() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override {};
		virtual String GetName() const  override final;
		virtual THandle<ISerializable> GetEditableObject() override;
	};

	class MeshNode_MeshParamWaterPatchData : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_MeshParamWaterPatchData, SimpleNode );

	public:
		MeshNode_MeshParamWaterPatchData( StructureTree& parentTree, THandle<CMesh>& mesh );
		virtual void CreateNodes() override final {};
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override;

		THandle<CMesh> m_mesh;
	};

	class MeshNode_MeshParamDeformableShapesData : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_MeshParamDeformableShapesData, SimpleNode );

	public:
		MeshNode_MeshParamDeformableShapesData( StructureTree& parentTree, THandle<CMesh>& mesh );
		virtual void CreateNodes() override final {};
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override;

		THandle<CMesh> m_mesh;
	};

	//-------------------------------------------------------------------------

	// helper interface for physics nodes
	class BACKEND_MESH_API IMeshPhysicsStructureNode : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED( IMeshPhysicsStructureNode, IMeshStructureNode );

	public:
		IMeshPhysicsStructureNode( StructureTree& parentTree );
	
		virtual void OnPhysicsObjectAdded( const THandle<physics::PhysicsBase>& object )
		{};

		virtual void OnPhysicsObjectRemoved()
		{};

		// node's general property has changed
		virtual void OnPhysicsObjectPropertyChanged( const THandle<physics::PhysicsBase>& object, const rtti::AccessPath& path )
		{}

		virtual void OnPhysicsObjectSelected( const THandle<physics::PhysicsBase>& object );

		virtual void RegisterForNotifications() {};
	};

	//-------------------------------------------------------------------------

	class BACKEND_MESH_API MeshNode_PhysicsSystem : public IMeshPhysicsStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_PhysicsSystem, IMeshPhysicsStructureNode );

	public:
		MeshNode_PhysicsSystem( StructureTree& parentTree, const THandle< physics::PhysicsEditorScene >& system, const String& name );
		~MeshNode_PhysicsSystem();
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;
		const THandle< physics::PhysicsEditorScene >& GetEditorScene();
		virtual void OnPhysicsObjectAdded( const THandle<physics::PhysicsBase>& object ) override final;
		virtual void OnPhysicsObjectRemoved( ) override final;
		virtual void RegisterForNotifications() override final;

		Bool IsBeingEdited();
	private:
		THandle< physics::PhysicsEditorScene > m_editorScene;
		String m_name;
	};

	//-------------------------------------------------------------------------

	class MeshNode_PhysicsSystemBody : public IMeshPhysicsStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_PhysicsSystemBody, IMeshPhysicsStructureNode );

	public:
		MeshNode_PhysicsSystemBody( StructureTree& parentTree, MeshNode_PhysicsSystem* system, physics::PhysicsActorPtr body );
		virtual ~MeshNode_PhysicsSystemBody();
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;
		virtual Bool OnDragAndDrop( ITreeNode* target ) override final;
		const physics::PhysicsActorPtr& GetActor();
		virtual void OnPhysicsObjectAdded( const THandle<physics::PhysicsBase>& object ) override final;
		virtual void OnPhysicsObjectPropertyChanged( const THandle<physics::PhysicsBase>& object, const rtti::AccessPath& path ) override final;
		virtual void RegisterForNotifications() override final;
	private:
		MeshNode_PhysicsSystem* m_system;
		physics::PhysicsActorPtr m_actor;
	};

	//-------------------------------------------------------------------------

	class MeshNode_PhysicsSystemJoint : public IMeshPhysicsStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_PhysicsSystemJoint, IMeshPhysicsStructureNode );

	public:
		MeshNode_PhysicsSystemJoint( StructureTree& parentTree, MeshNode_PhysicsSystem* system, physics::PhysicsJointPtr joint );
		~MeshNode_PhysicsSystemJoint();
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;
		const physics::PhysicsJointPtr& GetJoint();
		virtual void OnPhysicsObjectPropertyChanged( const THandle<physics::PhysicsBase>& object, const rtti::AccessPath& path ) override final;
		virtual void RegisterForNotifications() override final;
	private:
		MeshNode_PhysicsSystem* m_system;
		physics::PhysicsJointPtr m_joint;
	};

	//-------------------------------------------------------------------------

	class BACKEND_MESH_API MeshNode_PhysicsSystemMesh : public IMeshPhysicsStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_PhysicsSystemMesh, IMeshPhysicsStructureNode );

	public:
		MeshNode_PhysicsSystemMesh( StructureTree& parentTree, physics::PhysicsMeshPtr mesh );
		~MeshNode_PhysicsSystemMesh();
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;
		virtual Bool OnDragAndDrop( ITreeNode* target ) override final;
		const physics::PhysicsMeshPtr& GetMesh();
		virtual void RegisterForNotifications() override final;
	private:
		physics::PhysicsMeshPtr m_mesh;
	};

	//-------------------------------------------------------------------------

	class MeshNode_PhysicsSystemShape : public IMeshPhysicsStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_PhysicsSystemShape, IMeshPhysicsStructureNode );

	public:
		MeshNode_PhysicsSystemShape( StructureTree& parentTree, MeshNode_PhysicsSystem* system, physics::PhysicsActorPtr body, physics::PhysicsShapePtr shape );
		~MeshNode_PhysicsSystemShape();
		virtual void CreateNodes() override final;
		virtual String GetName() const override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;
		virtual Bool OnDragAndDrop( ITreeNode* target ) override final;
		const physics::PhysicsShapePtr& GetShape();
		virtual void RegisterForNotifications() override final;
	private:
		MeshNode_PhysicsSystem* m_system;
		physics::PhysicsActorPtr m_actor;
		physics::PhysicsShapePtr m_shape;
	};

	//-------------------------------------------------------------------------

	class BACKEND_MESH_API MeshNode_PhysicsActorCOM : public IMeshPhysicsStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_PhysicsActorCOM, IMeshPhysicsStructureNode );

	public:
		MeshNode_PhysicsActorCOM( StructureTree& parentTree, MeshNode_PhysicsSystem* system, THandle< physics::EditorActorCOM > m_actorCOM );
		virtual void CreateNodes() override final;
		virtual String GetName() const  override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;
		void SetActorCOM( const THandle< physics::EditorActorCOM >& actorCOM );
	private:
		MeshNode_PhysicsSystem* m_system;
		THandle< physics::EditorActorCOM > m_actorCOM;
	};

	class BACKEND_MESH_API MeshNode_Cloth : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_Cloth, IMeshStructureNode );
		RED_BASE_CLASS(IMeshStructureNode);

	public:
		MeshNode_Cloth( StructureTree& parentTree );

		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const override final;
		virtual void CreateNodes() override final;
		virtual red::String GetName() const override final;
	};

	class BACKEND_MESH_API MeshNode_GraphicalCloth : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_GraphicalCloth, IMeshStructureNode );
		RED_BASE_CLASS(IMeshStructureNode);

	public:
		MeshNode_GraphicalCloth( StructureTree& parentTree );

		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const override final;
		virtual void CreateNodes() override final;
		virtual red::String GetName() const override final;
	};

	class BACKEND_MESH_API MeshNode_PhysicalCloth : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_PhysicalCloth, IMeshStructureNode );
		RED_BASE_CLASS(IMeshStructureNode);

	public:
		MeshNode_PhysicalCloth( StructureTree& parentTree );

		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization(IMeshVisualizationContainer* vis) const override final;
		virtual void CreateNodes() override final;
		virtual red::String GetName() const override final;
	};

	//-------------------------------------------------------------------------

	class MeshNode_Occluder : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED(MeshNode_Occluder, IMeshStructureNode);

	public:
		MeshNode_Occluder(StructureTree& parentTree, THandle<mesh::MeshParamOccluderData> occluderData);
		virtual void CreateNodes() override final;
		virtual String GetName() const override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;

	private:
		THandle<mesh::MeshParamOccluderData> m_occluderData;
	};

	
	//-------------------------------------------------------------------------

	class MeshNode_WorkspotOffsets : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_WorkspotOffsets, SimpleNode );

	public:
		MeshNode_WorkspotOffsets( StructureTree& parentTree, THandle< mesh::MeshParamWorkspotOffsets > offsets );
		virtual void CreateNodes() override final{};
		virtual String GetName() const override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;

	private:
		THandle< mesh::MeshParamWorkspotOffsets > m_offsets;
	};

	//-------------------------------------------------------------------------

	class MeshNode_ImportedSnapPoints : public IMeshStructureNode
	{
		DECLARE_INTEROP_DERIVED( MeshNode_ImportedSnapPoints, SimpleNode );

	public:
		MeshNode_ImportedSnapPoints( StructureTree& parentTree, THandle< mesh::MeshParamImportedSnapPoint > snapPoints );
		virtual void CreateNodes() override final{};
		virtual String GetName() const override final;
		virtual THandle< ISerializable > GetEditableObject() override final;
		virtual void SetupVisualization( IMeshVisualizationContainer* vis ) const override final;

	private:
		THandle< mesh::MeshParamImportedSnapPoint > m_snapPoints;
	};

} // tools