// Copyright (c) 2018 CD Projekt Red. All Rights Reserved.

#include "build.h"

#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialDefinition.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshUtils.h"
#include "../../../common/resourceMesh/include/meshParamOccluderData.h"
#include "../../../common/resourceMesh/include/meshParamWorkspotOffsets.h"
#include "../../../common/resourceMesh/include/meshParamImportedSnapPoint.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"
#include "../../../common/resourceMesh/include/meshParamCloth.h"
#include "../../../common/resourceMesh/include/meshParamWaterPatchData.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/physics/include/physXNatives.h"
#include "../../../common/physics/include/physicsCollisionFilterResource.h"
#include "../../../common/redConfig/include/configVar.h"

#include "../../backendEngine/include/editAction.h"

#include "meshStructureNodes.h"
#include "meshStructureVisualization.h"
#include "meshDataView.h"
#include "backendMeshPanelSharedData.h"
#include "backendMeshEditor.h"
#include "physicsEditorScene.h"
#include "physicsActor.h"
#include "physicsJoint.h"
#include "physicsMesh.h"
#include "../../backendEngine/include/resourceMetadata.h"

namespace
{
	Uint32 CountLODTriangles( const mesh::DataViewRO& meshView, const Uint32 lodIndex )
	{
		Uint32 numTriangles = 0u;
		const red::DynArray< Uint16 > lodChunkIndices{ meshView.GetLODChunkIndices( lodIndex ) };
		for ( Uint32 j = 0; j < meshView.GetLODChunkIndices( lodIndex ).Size(); j++ )
		{
			numTriangles += meshView.GetChunkTriangleCount( lodChunkIndices[ j ]);
		}
		return numTriangles;
	}
}

namespace Config
{
	TConfigVar< Int32 > cvEditorIndexLimit( "Editor/MeshEditor", "DebugStreamIndicesLimit", 100 );
}

namespace tools
{
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Remove physics parameters form a mesh.
	// Can remove one or two parameters at the same time, so we can use it to remove both custom and imported physics.
	class MeshAction_RemovePhysics : public IEditAction
	{
	public:
		MeshAction_RemovePhysics( StructureTree& tree, const THandle< mesh::MeshParameter >& paramToRemove1,
								  const THandle< mesh::MeshParameter >& paramToRemove2 = nullptr )
			: IEditAction()
			, m_tree( tree )
			, m_mesh( tree.GetMesh() )
		{
			m_paramsToRemove[ 0 ] = paramToRemove1;
			m_paramsToRemove[ 1 ] = paramToRemove2;
		}

		virtual String GetDescription() const override { return "Remove physics"; }

		virtual Bool Do() override
		{
			for( const THandle< mesh::MeshParameter >& paramToRemove : m_paramsToRemove )
			{
				if( paramToRemove != nullptr )
					m_mesh->RemoveParameter( paramToRemove );
			}
			m_tree.RefreshTree();
			return true;
		}

		virtual Bool Undo() override
		{
			for( const THandle< mesh::MeshParameter >& paramToRemove : m_paramsToRemove )
			{
				if( paramToRemove != nullptr )
					m_mesh->AddParameter( paramToRemove );
			}
			m_tree.RefreshTree();
			return true;
		}

		virtual Bool IsModification() const override final
		{
			return true;
		}

		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final
		{
			outResourcePaths.Insert( m_mesh->GetPath() );
		}

	private:
		StructureTree& m_tree;
		THandle< CMesh > m_mesh;
		THandle< mesh::MeshParameter > m_paramsToRemove[ 2 ];
	};

	//////////////////////////////////////////////////////////////////////////////////////////////


	//------------------------------------

	MeshNode_Root::MeshNode_Root( StructureTree& tree )
		: IMeshStructureNode( tree )
	{}

	void MeshNode_Root::CreateNodes()
	{
		GetTree().AddNode( red::CreateSharedPtr< MeshNode_Bounds >( GetTree(), GetTree().GetMesh()->GetBoundingBox() ), *this );
		GetTree().AddNode( red::CreateSharedPtr < MeshNode_Lods >( GetTree() ), *this );

		GetTree().AddNode( red::CreateSharedPtr < MeshNode_MeshParameters >( GetTree() ), *this );

		MeshPanelSharedData* panelSharedData = GetTree().GetMeshResourceEditor()->GetMeshPanelSharedData();
		THandle< physics::PhysicsEditorScene > physicsData = HandleFromPtr( panelSharedData->GetMeshEditorPhysicsScene() );
		auto sharedPtr = red::CreateSharedPtr < MeshNode_PhysicsSystem >( GetTree(), physicsData, "Physics" );
		sharedPtr->RegisterForNotifications();
		GetTree().AddNode( sharedPtr, *this );

		GetTree().AddNode( red::CreateSharedPtr < MeshNode_Materials >( GetTree() ), *this );
		GetTree().AddNode( red::CreateSharedPtr < MeshNode_Appearances >( GetTree() ), *this );

		if ( GetTree().GetMeshView().GetNumBones() > 0 )
			GetTree().AddNode( red::CreateSharedPtr < MeshNode_Bones >( GetTree() ), *this );

		if ( auto occParam = Cast< mesh::MeshParamOccluderData >( GetTree().GetMesh()->GetParameter( ClassID< mesh::MeshParamOccluderData >() ) ) )
		{
			GetTree().AddNode( red::CreateSharedPtr < MeshNode_Occluder >( GetTree(), occParam ), *this );
		}

		if ( auto workspotParam = Cast< mesh::MeshParamWorkspotOffsets >( GetTree().GetMesh()->GetParameter( ClassID< mesh::MeshParamWorkspotOffsets >() ) ) )
		{
			GetTree().AddNode( red::CreateSharedPtr < MeshNode_WorkspotOffsets >( GetTree(), workspotParam ), *this );
		}

		auto view = mesh::MakeView( GetTree().GetMesh() );
		if ( view.HasParameter<mesh::MeshParamCloth>() || view.HasParameter<mesh::MeshParamCloth_Graphical>() )
		{
			GetTree().AddNode( red::CreateSharedPtr < MeshNode_Cloth>( GetTree() ), *this );
		}
	}

	THandle< ISerializable > MeshNode_Root::GetEditableObject()
	{
		return GetTree().GetMesh();
	}

	void MeshNode_Root::SetupInterop( MeshNode_Root* lodsNode, ::interop::utils::Object& op )
	{
	}

	//-------

	class MeshBoundsSetup : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshBoundsSetup );
		RTTI_DECLARE_TYPE( MeshBoundsSetup );

	public:
		MeshBoundsSetup( const Box& box )
			: m_box( box )
		{
		}

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override final
		{
		}

	private:
		Box m_box;
	};

	MeshNode_Bounds::MeshNode_Bounds( StructureTree& parentTree, const Box& box )
		: IMeshStructureNode( parentTree )
		, m_box( box )
	{}

	void MeshNode_Bounds::CreateNodes()
	{
	}

	THandle< ISerializable > MeshNode_Bounds::GetEditableObject()
	{
		return CreateHandle< MeshBoundsSetup >( m_box );
	}

	void MeshNode_Bounds::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_Box >( m_box ) );
	}

	void MeshNode_Bounds::SetupInterop( MeshNode_Bounds* lodsNode, ::interop::utils::Object& op )
	{
	}

	//------------------------------------

	MeshNode_Lods::MeshNode_Lods( StructureTree& parentTree )
		: IMeshStructureNode( parentTree )
	{
	}

	void MeshNode_Lods::CreateNodes()
	{
		Bool lod0Removed = false;
		THandle< res::ResourceMetadata > metadata = res::ResourceMetadata::Get( GetTree().GetMesh() );
		if ( metadata && metadata->GetTags().Exist( RED_NAME_CONSTEXPR( "lod0Removed" ) ) )
		{
			lod0Removed = true;
		}

		const Uint32 numLods = GetTree().GetMeshView().GetNumLODs();
		for( Uint32 i = 0; i < numLods; ++i )
			GetTree().AddNode( red::CreateSharedPtr< MeshNode_LodLevel >( GetTree(), i, lod0Removed ), *this );
	}

	void MeshNode_Lods::SetupInterop( MeshNode_Lods* lodsNode, ::interop::utils::Object& op )
	{
	}

	//------------------------------------

	MeshNode_Materials::MeshNode_Materials( StructureTree& parentTree )
		: IMeshStructureNode( parentTree )
	{
	}

	void MeshNode_Materials::CreateNodes()
	{
		const Uint32 numMaterials = GetTree().GetMeshView().GetNumMaterials();
		for( Uint32 i = 0; i < numMaterials; ++i )
		{
			GetTree().AddNode( red::CreateSharedPtr< MeshNode_MaterialReference >( GetTree(), i, -1, -1 ), *this );
		}
	}

	void MeshNode_Materials::SetupInterop( MeshNode_Materials* lodsNode, ::interop::utils::Object& op )
	{
	}

	//------------------------------------

	MeshNode_Appearances::MeshNode_Appearances( StructureTree& parentTree )
		: IMeshStructureNode( parentTree )
	{
	}

	void MeshNode_Appearances::CreateNodes()
	{
		const Uint32 numAppearances = GetTree().GetMeshView().GetNumAppearances();
		for( Uint32 i = 0; i < numAppearances; ++i )
		{
			GetTree().AddNode( red::CreateSharedPtr< MeshNode_Appearance >( GetTree(), i ), *this );
		}
	}

	void MeshNode_Appearances::SetupInterop( MeshNode_Appearances* lodsNode, ::interop::utils::Object& op )
	{
	}

	//------------------------------------
	class MeshAppearanceSetup : public ISerializable
	{
		RTTI_DECLARE_TYPE( MeshAppearanceSetup );
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshAppearanceSetup );

	public:
		MeshAppearanceSetup( const THandle< mesh::MeshAppearance >& meshAppearance, const THandle< CMesh >& mesh, Uint32 appearanceIndex, StructureTree* tree )
			: m_meshAppearance( meshAppearance )
			, m_mesh( mesh )
			, m_appearanceIndex( appearanceIndex )
			, m_tree( tree )
		{
			m_name = meshAppearance->GetName();
			m_tags = meshAppearance->GetTags();

			Uint32 numChunks = meshAppearance->GetNumChunks();
			m_chunkMaterials.Resize( numChunks );
			for ( Uint32 i = 0; i < numChunks; ++i )
			{
				m_chunkMaterials[ i ] = meshAppearance->GetChunkMaterialName( i );
			}

			SetParent( meshAppearance.Get() );                           // We do this so customEditor works correctly on the editor side.
		}

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override final
		{
			ISerializable::OnPropertyPostChange( path, oldValue, value );

			mesh::DataView view( *m_mesh );

			rtti::PathParser parser( path );
			CName propName;
			if ( parser.EatName( propName ) )
			{
				if ( propName == RED_NAME_CONSTEXPR_NOREG( "chunkMaterials" ) )
				{
					Int32 chunkIndex = 0;
					if ( parser.EatIndex( chunkIndex ) )
					{
						const String materialNameStr = FromStringDirect< String >( value->GetSingleValue() );
						const CName materialName = RED_NAME( materialNameStr );
						
						view.SetAppearanceChunkMaterial( m_appearanceIndex, chunkIndex, materialName );
						view.Apply();

						if ( m_tree != nullptr )
							m_tree->RefreshMeshPreview();
					}
				}
				else if ( propName == RED_NAME_CONSTEXPR_NOREG( "tags" ) )
				{
					red::DynArray< CName > tags{ rend::PoolMesh() };

					if ( value->IsArray() )
					{
						Uint32 numTags = value->GetNumElements();
						tags.Reserve( numTags );
						for ( Uint32 i = 0; i < numTags; ++i )
						{
							rtti::ValuePtr tagValue = value->GetArrayElement( i );
							const String tagStr = FromStringDirect< String >( tagValue->GetSingleValue() );
							const CName tag = RED_NAME( tagStr );
							tags.PushBack( tag );
						}
						view.SetAppearanceTags( m_appearanceIndex, tags );
						view.Apply();
					}
					else
					{
						Int32 tagIndex = 0;
						if ( parser.EatIndex( tagIndex ) )
						{
							const String tagStr = FromStringDirect< String >( value->GetSingleValue() );
							const CName tag = RED_NAME( tagStr );
							tags = m_meshAppearance->GetTags();
							tags[tagIndex] = tag;
							view.SetAppearanceTags( m_appearanceIndex, tags );
							view.Apply();
						}
					}
				}
			}
		}

	private:
		CName m_name;
		red::DynArray< CName > m_chunkMaterials{ rend::PoolMesh() };
		red::DynArray< CName > m_tags{ rend::PoolMesh() };

		THandle< CMesh > m_mesh;
		THandle< mesh::MeshAppearance > m_meshAppearance;
		Uint32 m_appearanceIndex;

		StructureTree* m_tree;
	};

	MeshNode_Appearance::MeshNode_Appearance( StructureTree& parentTree, const Uint32 index )
		: IMeshStructureNode( parentTree )
		, m_index( index )
	{
	}

	void MeshNode_Appearance::CreateNodes()
	{
		const Uint32 numChunks = GetTree().GetMeshView().GetAppearanceChunkCount( m_index );
		for( Uint32 i = 0; i < numChunks; ++i )
		{
			GetTree().AddNode( red::CreateSharedPtr< MeshNode_AppearanceChunk >( GetTree(), i, m_index ), *this );
		}
	}

	String MeshNode_Appearance::GetName() const
	{
		return GetTree().GetMeshView().GetAppearanceName( m_index ).AsChar();
	}

	THandle< ISerializable > MeshNode_Appearance::GetEditableObject()
	{
		return CreateHandle< MeshAppearanceSetup >( GetTree().GetMeshView().GetAppearanceData( m_index ), GetTree().GetMesh(), m_index, &GetTree() );
	}

	void MeshNode_Appearance::SetupInterop( MeshNode_Appearance* lodsNode, ::interop::utils::Object& op )
	{
	}

	//------------------------------------

	MeshNode_AppearanceChunk::MeshNode_AppearanceChunk( StructureTree& parentTree, const Uint32 index, const Uint32 appearanceIndex )
		: IMeshStructureNode( parentTree )
		, m_index( index )
		, m_appearanceIndex( appearanceIndex )
		, m_materialNodes( red::PoolBackend() )
	{}

	void MeshNode_AppearanceChunk::CreateNodes()
	{
		const Uint32 numMaterials = GetTree().GetMeshView().GetAppearanceChunkMaterialCount( m_appearanceIndex, m_index );
		m_materialNodes.Clear();
		m_materialNodes.Reserve( numMaterials );

		for( Uint32 i = 0; i < numMaterials; ++i )
		{
			auto matNode = red::CreateSharedPtr< MeshNode_MaterialReference >( GetTree(), i, m_index, m_appearanceIndex );
			m_materialNodes.PushBack( matNode.Get() );
			GetTree().AddNode( matNode, *this );
		}
	}

	THandle< ISerializable > MeshNode_AppearanceChunk::GetEditableObject()
	{
		return nullptr;
	}

	void MeshNode_AppearanceChunk::SetupInterop( MeshNode_AppearanceChunk* lodsNode, ::interop::utils::Object& op )
	{
	}

	void MeshNode_AppearanceChunk::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		//vis->SetForcedLOD( m_lodIndex );
		//vis->AttachVisualization( RED_NEW( MeshVisualization_Triangles )(GetTree().GetMesh(), m_index, CName()) );
	}

	void MeshNode_AppearanceChunk::OnMaterialChanged()
	{
		for( IMeshStructureNode* matNode : m_materialNodes )
		{
			matNode->RefreshNodeCaption();
			matNode->RecreateNodes();
		}
	}

	//------------------------------------

	MeshNode_Bones::MeshNode_Bones( StructureTree& parentTree )
		: IMeshStructureNode( parentTree )
	{
	}

	void MeshNode_Bones::CreateNodes()
	{
		const Uint32 numBones = GetTree().GetMeshView().GetNumBones();
		for( Uint32 i = 0; i < numBones; ++i )
			GetTree().AddNode( red::CreateSharedPtr< MeshNode_BoneReference >( GetTree(), i ), *this );
	}

	void MeshNode_Bones::SetupInterop( MeshNode_Bones* lodsNode, ::interop::utils::Object& op )
	{
	}

	//------------------------------------

	class MeshLodSetup : public ISerializable
	{
		RTTI_DECLARE_TYPE( MeshLodSetup );
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshLodSetup );

	public:
		MeshLodSetup( const THandle< CMesh >& meshData, const Uint32 lodIndex, MeshNode_LodLevel* node )
			: m_mesh( meshData )
			, m_lodIndex( lodIndex )
			, m_node( node )
		{
			mesh::DataViewRO view( *meshData );
			m_distance = view.GetLODDistance( m_lodIndex );
			m_triangleCount = CountLODTriangles( view, m_lodIndex );
			m_vertexCount = mesh::utils::CountLODVertices( view, m_lodIndex );
			m_chunksCount = mesh::utils::CountLODChunks( view, m_lodIndex );
			m_materialsCount = mesh::utils::CountLODMaterials( view, m_lodIndex );
		}

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override final
		{
			if( path.ToString().BeginsWith( "distance" ) )
			{
				const Float dist = FromStringDirect< Float >( value->GetSingleValue() );

				mesh::DataView view( *m_mesh );
				view.SetLODDistance( m_lodIndex, dist );
				view.Apply();

				m_node->RefreshNodeCaption();
			}
		}

	private:
		Float m_distance;
		Uint32 m_triangleCount;
		Uint32 m_vertexCount;
		Uint32 m_chunksCount;
		Uint32 m_materialsCount;

		MeshNode_LodLevel* m_node;
		THandle< CMesh > m_mesh;
		Uint32 m_lodIndex;
	};

	MeshNode_LodLevel::MeshNode_LodLevel( StructureTree& parentTree, const Uint32 index, Bool lod0Removed )
		: IMeshStructureNode( parentTree )
		, m_index( index )
		, m_lod0Removed( lod0Removed )
	{
	}

	String MeshNode_LodLevel::GetName() const
	{
		const mesh::DataViewRO& view = GetTree().GetMeshView();
		Float distance = view.GetLODDistance( m_index );
		Uint32 triangleCount = CountLODTriangles( view, m_index );
		Uint32 vertexCount = mesh::utils::CountLODVertices( view, m_index );
		Uint32 chunksCount = mesh::utils::CountLODChunks( view, m_index );

		return String::Printf( "LOD%u: %3.1f,   %2u chunks, %4u tris, %4u verts",
			m_lod0Removed ? m_index + 1 : m_index, distance, chunksCount, triangleCount, vertexCount );
	}

	void MeshNode_LodLevel::CreateNodes()
	{
		const auto lodMask = 1 << m_index;

		const auto numChunks = GetTree().GetMeshView().GetNumChunks();
		for( Uint32 i = 0; i < numChunks; ++i )
		{
			const auto chunkLodMask = GetTree().GetMeshView().GetChunkLodMask( i );
			if( chunkLodMask & lodMask )
			{
				GetTree().AddNode( red::CreateSharedPtr< MeshNode_Chunk >( GetTree(), i, m_index ), *this );
			}
		}
	}

	THandle< ISerializable > MeshNode_LodLevel::GetEditableObject()
	{
		return CreateHandle< MeshLodSetup >( GetTree().GetMesh(), m_index, this );
	}

	void MeshNode_LodLevel::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->SetForcedLOD( m_index );
	}

	void MeshNode_LodLevel::SetupInterop( MeshNode_LodLevel* lodNode, ::interop::utils::Object& op )
	{
		op.RegisterFunctor< Bool >( "removeLOD",
									[lodNode]( interop::IContext& ctx ) {
										return ctx.ThrowException( "Based on recent discussions manual LODS are disabled" );
										/*Uint32 lodInd = lodNode->m_index;
			if (lodInd < lodNode->m_mesh->GetNumLODLevels())
			{
				if (lodNode->m_mesh->RemoveLOD(lodInd))
				{
					auto parent = lodNode->m_treeFallbackPtr->GetNode(lodNode->GetParentId());
					for (auto child : parent->GetChildren())
					{
						LodNode* node = static_cast<LodNode*>(child);
						if (node->GetLodIndex() > lodInd)
						{
							node->DecrementIndex();
						}
					}

					lodNode->m_treeFallbackPtr->NotifyLodChanged();
					lodNode->m_treeFallbackPtr->RemoveNode(lodNode->GetId());
					return true;
				}
				else
					ctx.ThrowException("LOD removal failed");
			}
			else

			return false;*/
									} );
	}

	//-------

	struct MeshChunkMaterialEntry
	{
		RTTI_DECLARE_TYPE( MeshChunkMaterialEntry );

		CName m_name;

		RED_FORCE_INLINE MeshChunkMaterialEntry( CName name = CName::NONE() )
			: m_name( name )
		{}
	};

	class MeshChunkSetup : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshChunkSetup );
		RTTI_DECLARE_TYPE( MeshChunkSetup );

	public:
		MeshChunkSetup( MeshNode_Chunk& parentNode )
			: m_parentNode( &parentNode )
		{
			Uint32 chunkIndex = m_parentNode->GetChunkIndex();
			mesh::DataViewRO view( *GetMesh() );
			m_numBonesPerVertex = view.GetChunkNumBonesPerVertex( chunkIndex );
			m_triangleCount = view.GetChunkTriangleCount( chunkIndex );
			m_vertexCount = view.GetChunkVertexCount( chunkIndex );
			m_vertexType = view.GetChunkVertexType( chunkIndex );
			m_flags = view.GetChunkFlags( chunkIndex );
			m_sourceMeshUUID = view.GetChunkSourceMeshUUID( chunkIndex );
			m_sourceMaterialUUID = view.GetChunkSourceMaterialUUID( chunkIndex );
		}

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override final
		{
			const auto& name = path.ToString();
			if ( name.BeginsWith( "flags" ) )
			{
				::mesh::DataView view( *GetMesh() );

				view.SetChunkFlags( m_parentNode->GetChunkIndex(), m_flags );
				view.Apply();

				m_parentNode->RefreshNodeCaption();
				m_parentNode->RefreshParentNodeCaption();
				m_parentNode->GetTree().RefreshMeshPreview();
			}
		}

		RED_FORCE_INLINE const THandle< CMesh >& GetMesh() const
		{
			return m_parentNode->GetTree().GetMesh();
		}

	private:
		EMeshVertexType m_vertexType;
		mesh::ChunkFlags m_flags;
		Uint8 m_numBonesPerVertex;
		Uint32 m_vertexCount;
		Uint32 m_triangleCount;
		String m_sourceMeshUUID;
		String m_sourceMaterialUUID;
		MeshNode_Chunk* m_parentNode;
	};

	class CustomPropertyEditor_MeshChunkMaterial : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( CustomPropertyEditor_MeshChunkMaterial );

	public:
		CustomPropertyEditor_MeshChunkMaterial()
			: m_materialNames( red::PoolBackend() )
		{
		}

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* /*root*/ ) const override final
		{
			return customTypeName == RED_NAME_CONSTEXPR_NOREG( "MeshChunkMaterial" );
		}

		virtual Bool HasOptions() const override final
		{
			return true;
		}

		virtual const red::DynArray< String >& CustomPropertyEditor_MeshChunkMaterial::GetOptions() const
		{
			if( m_materialNames.Empty() )
			{
				auto chunkInfo = FindParentObject< MeshChunkSetup >();
				if( chunkInfo )
				{
					auto meshData = chunkInfo->GetMesh();
					const ::mesh::DataViewRO view( *meshData );

					for( Uint32 i = 0; i < view.GetNumMaterials(); ++i )
					{
						const auto name = view.GetMaterialName( i );
						m_materialNames.PushBack( name.AsChar() );
					}
				}
			}

			return m_materialNames;
		}

		virtual CName GetControlType() const override final
		{
			return RED_NAME( red::String::Printf( "%hs:Restricted", ::GetTypeName< CName >().AsChar() ) );
		}

	private:
		mutable red::DynArray< red::String > m_materialNames;
	};


	MeshNode_Chunk::MeshNode_Chunk( StructureTree& parentTree, const Uint32 index, const Uint32 lodIndex )
		: IMeshStructureNode( parentTree )
		, m_index( index )
		, m_lodIndex( lodIndex )
	{}

	void MeshNode_Chunk::CreateNodes()
	{
		// Streams
		GetTree().AddNode( red::CreateSharedPtr< MeshNode_ChunkStreams >( GetTree(), m_index, m_lodIndex ), *this );

		// Skinning
		const auto vertexType = GetTree().GetMeshView().GetChunkVertexType( m_index );
		if( IsSkinnedMeshVertexType( vertexType ) )
		{
			GetTree().AddNode( red::CreateSharedPtr< MeshNode_ChunkSkinning >( GetTree(), m_index, m_lodIndex ), *this );
		}
	}

	THandle< ISerializable > MeshNode_Chunk::GetEditableObject()
	{
		return CreateHandle< MeshChunkSetup >( *this );
	}

	void MeshNode_Chunk::SetupInterop( MeshNode_Chunk* lodsNode, ::interop::utils::Object& op )
	{
	}

	void MeshNode_Chunk::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->SetForcedLOD( m_lodIndex );
		vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_Triangles >( GetTree().GetMesh(), m_index, CName() ) );
	}

	String MeshNode_Chunk::GetName() const
	{
		const mesh::DataViewRO& meshView = GetTree().GetMeshView();
		Uint32 triangleCount = meshView.GetChunkTriangleCount( m_index );
		Uint32 vertexCount = meshView.GetChunkVertexCount( m_index );
		return String::Printf( "Chunk%u:   %u tris, %u verts", m_index, triangleCount, vertexCount );
	}

	//------------------------------------

	MeshNode_ChunkSkinning::MeshNode_ChunkSkinning( StructureTree& parentTree, const Uint32 index, const Uint32 lodIndex )
		: IMeshStructureNode( parentTree )
		, m_chunkIndex( index )
		, m_lodIndex( lodIndex )
	{
	}

	void MeshNode_ChunkSkinning::CreateNodes()
	{
		// count how many vertices use given bone
		Uint32 boneMap[ 256 ]; // bone indices are 8-bit
		red::Memset( boneMap, 0, sizeof( boneMap ) );

		const mesh::StreamViewRO< Color > dataBoneIndices( GetTree().GetMeshView().GetChunkStreamData( m_chunkIndex, MST_SkinningIndices_4U8 ) );
		const mesh::StreamViewRO< Vector4 > dataBoneWeights( GetTree().GetMeshView().GetChunkStreamData( m_chunkIndex, MST_SkinningWeights_4F ) );
		if( dataBoneWeights.IsValid() && dataBoneIndices.IsValid() )
		{
			for( Uint32 j = 0; j < dataBoneIndices.Size(); ++j )
			{
				auto indices = dataBoneIndices[ j ];
				auto weights = dataBoneWeights[ j ];

				for( Uint32 i = 0; i < 4; ++i )
				{
					if( weights[ i ] > 0.f )
					{
						boneMap[ indices.RGBA[ i ] ]++;
					}
				}
			}
		}

		const mesh::StreamViewRO< Color > dataBoneIndicesExt( GetTree().GetMeshView().GetChunkStreamData( m_chunkIndex, MST_SkinningIndicesExt_4U8 ) );
		const mesh::StreamViewRO< Vector4 > dataBoneWeightsExt( GetTree().GetMeshView().GetChunkStreamData( m_chunkIndex, MST_SkinningWeightsExt_4F ) );
		if( dataBoneWeightsExt.IsValid() && dataBoneIndicesExt.IsValid() )
		{
			for( Uint32 j = 0; j < dataBoneIndicesExt.Size(); ++j )
			{
				auto indices = dataBoneIndicesExt[ j ];
				auto weights = dataBoneWeightsExt[ j ];

				for( Uint32 i = 0; i < 4; ++i )
				{
					if( weights[ i ] > 0.f )
					{
						boneMap[ indices.RGBA[ i ] ]++;
					}
				}
			}
		}

		for( Uint32 i = 0; i < RED_ARRAY_COUNT( boneMap ); ++i )
		{
			if( boneMap[ i ] > 0 )
			{
				GetTree().AddNode( red::CreateSharedPtr< MeshNode_BoneInfluences >( GetTree(), m_chunkIndex, i, boneMap[ i ], m_lodIndex ), *this );
			}
		}
	}

	void MeshNode_ChunkSkinning::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->SetForcedLOD( m_lodIndex );
	}

	void MeshNode_ChunkSkinning::SetupInterop( MeshNode_ChunkSkinning* lodsNode, ::interop::utils::Object& op )
	{
	}

	//------------------------------------

	MeshNode_ChunkStreams::MeshNode_ChunkStreams( StructureTree& parentTree, const Uint32 index, const Uint32 lodIndex )
		: IMeshStructureNode( parentTree )
		, m_chunkIndex( index )
		, m_lodIndex( lodIndex )
	{
	}

	void MeshNode_ChunkStreams::CreateNodes()
	{
		const Uint64 streamMask = GetTree().GetMeshView().GetChunkStreamMask( m_chunkIndex );
		for( Uint32 i = 0; i < 64; ++i )
		{
			const Uint64 testMask = (Uint64)1 << i;
			if( 0 != ( streamMask & testMask ) )
			{
				GetTree().AddNode( red::CreateSharedPtr< MeshNode_ChunkStream >( GetTree(), m_chunkIndex, m_lodIndex, (EMeshStreamType)testMask ), *this );
			}
		}
		if ( ( streamMask & MST_GarmentFlags_U32 ) != 0 )
			GetTree().AddNode( red::CreateSharedPtr< MeshNode_ChunkStream_GarmentCaps >(GetTree(), m_chunkIndex, m_lodIndex), *this );
	}

	void MeshNode_ChunkStreams::SetupInterop( MeshNode_ChunkStreams* lodsNode, ::interop::utils::Object& op )
	{
	}

	void MeshNode_ChunkStreams::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->SetForcedLOD( m_lodIndex );
	}

	///-----

	struct VertexBoneIndices
	{
		RTTI_DECLARE_TYPE( VertexBoneIndices );

		Uint8 m_boneIndex0;
		Uint8 m_boneIndex1;
		Uint8 m_boneIndex2;
		Uint8 m_boneIndex3;
	};

	///-----

	class MeshStreamVec4 : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshStreamVec4 );
		RTTI_DECLARE_TYPE( MeshStreamVec4 );

	public:
		MeshStreamVec4( const Uint32 size )
			: m_data( size, red::PoolBackend() ){};
		red::DynArray< Vector4 > m_data;

		typedef Vector4 DATA;
	};

	class MeshStreamVec3 : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshStreamVec3 );
		RTTI_DECLARE_TYPE( MeshStreamVec3 );

	public:
		MeshStreamVec3( const Uint32 size )
			: m_data( size, red::PoolBackend() ){};
		red::DynArray< Vector3 > m_data;

		typedef Vector3 DATA;
	};

	class MeshStreamVec2 : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshStreamVec2 );
		RTTI_DECLARE_TYPE( MeshStreamVec2 );

	public:
		MeshStreamVec2( const Uint32 size )
			: m_data( size, red::PoolBackend() ){};
		red::DynArray< Vector2 > m_data;

		typedef Vector2 DATA;
	};

	class MeshStreamFloat : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshStreamFloat );
		RTTI_DECLARE_TYPE( MeshStreamFloat );

	public:
		MeshStreamFloat( const Uint32 size )
			: m_data( size, red::PoolBackend() ){};
		red::DynArray< Float > m_data;

		typedef Float DATA;
	};

	class MeshStreamColor : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshStreamColor );
		RTTI_DECLARE_TYPE( MeshStreamColor );

	public:
		MeshStreamColor( const Uint32 size )
			: m_data( size, red::PoolBackend() ){};
		red::DynArray< Color > m_data;

		typedef Color DATA;
	};

	class MeshStreamUint16 : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshStreamUint16 );
		RTTI_DECLARE_TYPE( MeshStreamUint16 );

	public:
		MeshStreamUint16( const Uint32 size )
			: m_data( size, red::PoolBackend() ){};
		red::DynArray< Uint16 > m_data;

		typedef Uint16 DATA;
	};

	class MeshStreamUint32 : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshStreamUint32 );
		RTTI_DECLARE_TYPE( MeshStreamUint32 );

	public:
		MeshStreamUint32( const Uint32 size )
			: m_data( size, red::PoolBackend() ){};
		red::DynArray< Uint32 > m_data;

		typedef Uint32 DATA;
	};

	class MeshStreamBoneIndices : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshStreamBoneIndices );
		RTTI_DECLARE_TYPE( MeshStreamBoneIndices );

	public:
		MeshStreamBoneIndices( const Uint32 size )
			: m_data( size, red::PoolBackend() ){};
		red::DynArray< VertexBoneIndices > m_data;

		typedef VertexBoneIndices DATA;
	};

	class MeshStreamGarmentCaps : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshStreamGarmentCaps );
		RTTI_DECLARE_TYPE( MeshStreamGarmentCaps );

	public:
		MeshStreamGarmentCaps( const Uint32 size )
			: m_data( size, red::PoolBackend() ), m_numVerticesMarkedAsCaps(0), m_numVerticesNonCaps(0), m_numTrianglesMarkedAsCaps(0), m_numTrianglesPartiallyMarkedAsCaps(0), m_numTrianglesNonCaps(0) {};
		red::DynArray< Bool > m_data;
		Uint32 m_numVerticesMarkedAsCaps;
		Uint32 m_numVerticesNonCaps;
		Uint32 m_numTrianglesMarkedAsCaps;
		Uint32 m_numTrianglesPartiallyMarkedAsCaps;
		Uint32 m_numTrianglesNonCaps;

		typedef Bool DATA;
	};

	template< typename RetData >
	static THandle< RetData > ExtractData( const mesh::DataViewRO& data, const Int32 chunkIndex, const EMeshStreamType stream, Uint32 limit )
	{
		mesh::StreamIteratorRO< typename RetData::DATA > it( data.GetChunkStreamData( chunkIndex, stream ) );
		if( !it.IsValid() )
			return nullptr;

		Uint32 numElems = it.Size();
		if ( limit != -1 && numElems > limit )
		{
			numElems = limit;
		}

		THandle< RetData > ret( RED_NEW( RetData )( numElems ) );
		auto* writePtr = ret->m_data.TypedData();
		for( Uint32 i = 0; i < numElems; ++i )
			*( writePtr++ ) = *( it++ );

		return ret;
	}

	template<>
	static THandle< MeshStreamBoneIndices > ExtractData( const mesh::DataViewRO& data, const Int32 chunkIndex, const EMeshStreamType stream, Uint32 limit )
	{
		mesh::StreamIteratorRO< Uint8 > it( data.GetChunkStreamData( chunkIndex, stream ) );
		if( !it.IsValid() )
			return nullptr;

		Uint32 numElems = it.Size() / 4u;
		if ( limit != -1 && numElems > ( limit / 4u ) )
		{
			numElems = limit / 4u;
		}


		THandle< MeshStreamBoneIndices > ret( RED_NEW( MeshStreamBoneIndices )( numElems ) );
		VertexBoneIndices* writePtr = ret->m_data.TypedData();
		for( Uint32 i = 0; i < numElems; ++i, ++writePtr )
		{
			writePtr->m_boneIndex0 = *( it++ );
			writePtr->m_boneIndex1 = *( it++ );
			writePtr->m_boneIndex2 = *( it++ );
			writePtr->m_boneIndex3 = *( it++ );
		}

		return ret;
	}

	static THandle< MeshStreamGarmentCaps > ExtractDataGarmentCaps( const mesh::DataViewRO& data, const Int32 chunkIndex, Uint32 limit )
	{
		mesh::StreamIteratorRO< Uint32 > it( data.GetChunkStreamData( chunkIndex, MST_GarmentFlags_U32 ) );
		if ( !it.IsValid() )
			return nullptr;

		Uint32 numVerts = it.Size();
		Uint32 numElems = numVerts;
		if ( limit != -1 && numElems > limit )
		{
			numElems = limit;
		}

		red::DynArray< Bool > vertexCapFlags;
		vertexCapFlags.Resize( numVerts );

		THandle< MeshStreamGarmentCaps > ret( RED_NEW( MeshStreamGarmentCaps )(numElems) );
		auto* writePtr = ret->m_data.TypedData();
		for ( Uint32 i = 0; i < numVerts; ++i )
		{
			Bool vertexMarkedAsCap = ((*(it++) & 256u) != 0);
			vertexCapFlags[i] = vertexMarkedAsCap;
			if ( i < numElems )
				*(writePtr++) = vertexMarkedAsCap;
			if ( vertexMarkedAsCap )
				++ret->m_numVerticesMarkedAsCaps;
		}

		mesh::ChunkStreamDataRO chunkIndexStream = data.GetChunkStreamData( chunkIndex, MST_Index_U16 );
		red::ArraySpan< const Uint16 > chunkIndices = mesh::MakeSpan< const Uint16 >( chunkIndexStream );
		Uint32 numTrians = chunkIndices.Size() / 3u;

		for ( Uint32 i = 0; i < numTrians; ++i )
		{
			Uint32 ind0 = chunkIndices[i * 3u + 0u];
			Uint32 ind1 = chunkIndices[i * 3u + 1u];
			Uint32 ind2 = chunkIndices[i * 3u + 2u];
			Uint32 numCapTrianVerts = (Uint32)vertexCapFlags[ind0] + vertexCapFlags[ind1] + vertexCapFlags[ind2];
			if ( numCapTrianVerts == 0 )
				++ret->m_numTrianglesNonCaps;
			else if ( numCapTrianVerts == 3 )
				++ret->m_numTrianglesMarkedAsCaps;
			else
				++ret->m_numTrianglesPartiallyMarkedAsCaps;
		}

		ret->m_numVerticesNonCaps = numVerts - ret->m_numVerticesMarkedAsCaps;

		return ret;
	}


	MeshNode_ChunkStream::MeshNode_ChunkStream( StructureTree& parentTree, const Uint32 index, const Uint32 lodIndex, EMeshStreamType stream )
		: IMeshStructureNode( parentTree )
		, m_chunkIndex( index )
		, m_lodIndex( lodIndex )
		, m_stream( stream )
	{
	}

	String MeshNode_ChunkStream::GetName() const
	{
		switch( m_stream )
		{
		case MST_Position_3F:
			return "Position (Float3)";
		case MST_SkinningIndices_4U8:
			return "SkinningIndices (Byte4)";
		case MST_SkinningWeights_4F:
			return "SkinningWeights (Float4)";
		case MST_SkinningIndicesExt_4U8:
			return "SkinningIndicesExt (Byte4)";
		case MST_SkinningWeightsExt_4F:
			return "SkinningWeightsExt (Float4)";
		case MST_Color_U32:
			return "Color (Color)";
		case MST_TexCoord0_2F:
			return "TexCoord0 (Float2)";
		case MST_TexCoord1_2F:
			return "TexCoord1 (Float2)";
		case MST_Normal_3F:
			return "Normal (Float3)";
		case MST_Tangent_3F:
			return "Tangent (Float3)";
		case MST_Binormal_3F:
			return "Binormal (Float3)";
		case MST_DestructionIndices_2U16:
			return "DestrGroup (Short2)";
		case MST_Multilayer_1F:
			return "Multilayer (Float)";
		case MST_Index_U16:
			return "Indices (Uint16)";
		case MST_MorphOffset_3F:
			return "Morph offset (Float3)";
		case MST_GarmentFlags_U32:
			return "Garment flags (Uint)";
		case MST_VehicleDmgNormalFront_3F:
			return "Vehicle damage normal F (Float3)";
		case MST_VehicleDmgNormalSides_3F:
			return "Vehicle damage normal S (Float3)";
		case MST_VehicleDmgPosFront_3F:
			return "Vehicle damage position F (Float3)";
		case MST_VehicleDmgPosSides_3F:
			return "Vehicle damage position S (Float3)";
		case MST_MorphVertexData_3F:
			return "Morph vertex data (Float3(u, v, 0))";
		case MST_LightBlockerIntensity_1F:
			return "Light blocker intensity (Float)";
		}

		return "Unknown";
	}

	THandle< ISerializable > MeshNode_ChunkStream::GetEditableObject()
	{
		Int32 value = Config::cvEditorIndexLimit.Get();
		Uint32 indexLimit = value > 0 ? value : - 1;
		Uint32 boneIndexLimit = indexLimit * 4;

		switch( m_stream )
		{
		case MST_Position_3F:
			return ExtractData< MeshStreamVec3 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_SkinningIndices_4U8:
			return ExtractData< MeshStreamBoneIndices >( GetTree().GetMeshView(), m_chunkIndex, m_stream, boneIndexLimit );
		case MST_SkinningWeights_4F:
			return ExtractData< MeshStreamVec4 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_SkinningIndicesExt_4U8:
			return ExtractData< MeshStreamBoneIndices >( GetTree().GetMeshView(), m_chunkIndex, m_stream, boneIndexLimit );
		case MST_SkinningWeightsExt_4F:
			return ExtractData< MeshStreamVec4 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_Color_U32:
			return ExtractData< MeshStreamColor >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_TexCoord0_2F:
			return ExtractData< MeshStreamVec2 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_TexCoord1_2F:
			return ExtractData< MeshStreamVec2 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_Normal_3F:
			return ExtractData< MeshStreamVec3 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_Tangent_3F:
			return ExtractData< MeshStreamVec3 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_Binormal_3F:
			return ExtractData< MeshStreamVec3 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_Index_U16:
			return ExtractData< MeshStreamUint16 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_Multilayer_1F:
			return ExtractData< MeshStreamFloat >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_MorphOffset_3F:
			return ExtractData< MeshStreamVec3 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_GarmentFlags_U32:
			return ExtractData< MeshStreamUint32 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_VehicleDmgNormalFront_3F:
			return ExtractData< MeshStreamVec3 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_VehicleDmgNormalSides_3F:
			return ExtractData< MeshStreamVec3 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_VehicleDmgPosFront_3F:
			return ExtractData< MeshStreamVec3 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_VehicleDmgPosSides_3F:
			return ExtractData< MeshStreamVec3 >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_MorphVertexData_3F:
			return ExtractData< MeshStreamFloat >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		case MST_LightBlockerIntensity_1F:
			return ExtractData< MeshStreamFloat >( GetTree().GetMeshView(), m_chunkIndex, m_stream, indexLimit );
		}

		return nullptr;
	}

	void MeshNode_ChunkStream::SetupInterop( MeshNode_ChunkStream* lodsNode, ::interop::utils::Object& op )
	{
	}

	void MeshNode_ChunkStream::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->SetForcedLOD( m_lodIndex );

		if( m_stream == MST_Position_3F )
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamPositions >( GetTree().GetMesh(), m_chunkIndex ) );
		else if( m_stream == MST_TexCoord0_2F )
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamTexCoord0 >( GetTree().GetMesh(), m_chunkIndex ) );
		else if( m_stream == MST_TexCoord1_2F )
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamTexCoord1 >( GetTree().GetMesh(), m_chunkIndex ) );
		else if( m_stream == MST_Color_U32 )
		{
			vis->SetMeshVisibility( false );
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamColor >( GetTree().GetMesh(), m_chunkIndex ) );
		}
		else if( m_stream == MST_Normal_3F )
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamNormals >( GetTree().GetMesh(), m_chunkIndex ) );
		else if( m_stream == MST_Tangent_3F )
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamTangents >( GetTree().GetMesh(), m_chunkIndex ) );
		else if( m_stream == MST_Binormal_3F )
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamBitangents >( GetTree().GetMesh(), m_chunkIndex ) );
		else if( m_stream == MST_MorphOffset_3F )
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamMorphOffsets >( GetTree().GetMesh(), m_chunkIndex ) );
		else if( m_stream == MST_GarmentFlags_U32 )
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamGarmentFlags >( GetTree().GetMesh(), m_chunkIndex ) );
		else if( m_stream == MST_Index_U16 )
		{
			vis->SetMeshVisibility( false );
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamIndices >( GetTree().GetMesh(), m_chunkIndex ) );
		}
		else if ( m_stream == MST_MorphVertexData_3F )
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamMorphVertexData >( GetTree().GetMesh(), m_chunkIndex ) );
	}

	///-----

	MeshNode_ChunkStream_GarmentCaps::MeshNode_ChunkStream_GarmentCaps( StructureTree& parentTree, const Uint32 index, const Uint32 lodIndex )
		: IMeshStructureNode( parentTree )
		, m_chunkIndex( index )
		, m_lodIndex( lodIndex )
	{
	}

	String MeshNode_ChunkStream_GarmentCaps::GetName() const
	{
		return "Garment Caps (Bool)";
	}

	THandle< ISerializable > MeshNode_ChunkStream_GarmentCaps::GetEditableObject()
	{
		Int32 value = Config::cvEditorIndexLimit.Get();
		Uint32 indexLimit = value > 0 ? value : -1;
		Uint32 boneIndexLimit = indexLimit * 4;

		return ExtractDataGarmentCaps( GetTree().GetMeshView(), m_chunkIndex, indexLimit );
	}

	void MeshNode_ChunkStream_GarmentCaps::SetupInterop( MeshNode_ChunkStream_GarmentCaps* lodsNode, ::interop::utils::Object& op )
	{
	}

	void MeshNode_ChunkStream_GarmentCaps::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->SetForcedLOD( m_lodIndex );
		vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_StreamGarmentCaps >( GetTree().GetMesh(), m_chunkIndex ) );
	}

	///-----

	MeshNode_TextureReference::MeshNode_TextureReference( StructureTree& parentTree, THandle< ITexture > texture, const String& paramName )
		: IMeshStructureNode( parentTree )
		, m_paramName( paramName )
		, m_texture( texture )
	{}

	String MeshNode_TextureReference::GetName() const
	{
		String ret = m_paramName;
		if( m_texture )
		{
			ret += ": ";
			ret += red::paths::ExtractParentPath( m_texture->GetPath().ToStringView() );
		}
		return ret;
	}

	THandle< ISerializable > MeshNode_TextureReference::GetEditableObject()
	{
		return m_texture;
	}

	void MeshNode_TextureReference::SetupInterop( MeshNode_TextureReference* lodsNode, ::interop::utils::Object& op )
	{
	}

	void MeshNode_TextureReference::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		auto bitmap = Cast< CBitmapTexture >( m_texture );
		if( bitmap )
			vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_Texture >( bitmap ) );
	}

	///-----

	MeshNode_MaterialReference::MeshNode_MaterialReference( StructureTree& parentTree, const Uint32 materialInd, const Int32 chunkIndex, const Int32 appearanceIndex )
		: IMeshStructureNode( parentTree )
		, m_materialInd( materialInd )
		, m_chunkIndex( chunkIndex )
		, m_appearanceIndex( appearanceIndex )
	{
	}

	void MeshNode_MaterialReference::CreateNodes()
	{
		auto index = GetMeshMaterialIndex();
		if( auto material = GetTree().GetMeshView().GetMaterialData( index ) )
		{
			if( auto* baseDef = material->GetMaterialDefinition() )
			{
				for( const auto& param : baseDef->GetParameterDefinitions( MSH_PixelShader ) )
				{
					if( param.m_type != IMaterialDataProviderDesc::PT_Texture )
						continue;

					TResRef< ITexture > texture;
					if( material->ReadParameter( param.m_name, texture ) && texture )
					{
						GetTree().AddNode( red::CreateSharedPtr< MeshNode_TextureReference >( GetTree(), texture.Get(), param.m_name.AsChar() ), *this );
					}
				}
			}
		}
	}

	String MeshNode_MaterialReference::GetName() const
	{
		return GetMeshMaterialName().AsChar();
	}

	CName MeshNode_MaterialReference::GetMeshMaterialName() const
	{
		const mesh::DataViewRO& meshView = GetTree().GetMeshView();
		if( m_chunkIndex < 0 )
			return meshView.GetMaterialName( m_materialInd );
		return meshView.GetAppearanceMaterialName( m_appearanceIndex, m_chunkIndex, m_materialInd );
	}

	THandle< ISerializable > MeshNode_MaterialReference::GetEditableObject()
	{
		auto index = GetMeshMaterialIndex();
		auto material = GetTree().GetMeshView().GetMaterialData( index );
		if( material && material->IsA< CMaterialInstance >() )
			return material;

		return nullptr;
	}

	void MeshNode_MaterialReference::SetupInterop( MeshNode_MaterialReference* lodsNode, ::interop::utils::Object& op )
	{
		op.RegisterFunctor< void >( "Remove", [lodsNode]( interop::IContext& ctx ) {
			StructureTree& tree = lodsNode->GetTree();
			const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
			if( editorFallback->RemoveMaterial( lodsNode->GetMeshMaterialIndex() ) )
			{
				tree.RefreshTree();
			}
		} );
	}

	void MeshNode_MaterialReference::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_Triangles >( GetTree().GetMesh(), m_chunkIndex, RED_NAME( GetName() ) ) );
	}

	Uint32 MeshNode_MaterialReference::GetMeshMaterialIndex() const
	{
		if( m_chunkIndex < 0 )
			return m_materialInd;

		return mesh::utils::FindMaterialIndex( GetTree().GetMeshView(), GetMeshMaterialName() );
	}

	///-----

	class MeshBoneSetup : public ISerializable
	{
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshBoneSetup );
		RTTI_DECLARE_TYPE( MeshBoneSetup );

	public:
		MeshBoneSetup( const THandle< CMesh >& meshData, const Uint32 boneIndex )
			: m_mesh( meshData )
			, m_boneIndex( boneIndex )
		{
			const mesh::DataViewRO view( *meshData );
			m_name = view.GetBoneName( boneIndex );
			m_rig = view.GetBoneMatrix( boneIndex );
			m_epsilon = view.GetBoneVertexEpsilon( boneIndex );
		}

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override final
		{
		}

	private:
		THandle< CMesh > m_mesh;
		Uint32 m_boneIndex;

		CName m_name;
		Matrix m_rig;
		Float m_epsilon;
	};

	MeshNode_BoneReference::MeshNode_BoneReference( StructureTree& parentTree, const Uint32 boneIndex )
		: IMeshStructureNode( parentTree )
		, m_boneIndex( boneIndex )
	{}

	String MeshNode_BoneReference::GetName() const
	{
		const auto& boneName = GetTree().GetMeshView().GetBoneName( m_boneIndex );
		return boneName.AsChar();
	}

	THandle< ISerializable > MeshNode_BoneReference::GetEditableObject()
	{
		return CreateHandle< MeshBoneSetup >( GetTree().GetMesh(), m_boneIndex );
	}

	void MeshNode_BoneReference::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_BoneReference >( GetTree().GetMesh(), m_boneIndex ) );
	}

	void MeshNode_BoneReference::SetupInterop( MeshNode_BoneReference* lodsNode, ::interop::utils::Object& op )
	{
	}

	void MeshNode_BoneReference::CreateNodes()
	{
		GetTree().AddNode( red::CreateSharedPtr< MeshNode_BoneInfluences >( GetTree(), -1, m_boneIndex, 0, -1 ), *this );
	}

	//--

	MeshNode_BoneInfluences::MeshNode_BoneInfluences( StructureTree& parentTree, const Int32 chunkIndex, const Uint32 boneIndex, const Uint32 vertexCount, const Int32 lodIndex )
		: IMeshStructureNode( parentTree )
		, m_lodIndex( lodIndex )
		, m_chunkIndex( chunkIndex )
		, m_boneIndex( boneIndex )
		, m_vertexCount( vertexCount )
	{}

	String MeshNode_BoneInfluences::GetName() const
	{
		const auto& boneName = GetTree().GetMeshView().GetBoneName( m_boneIndex );

		if( m_vertexCount > 0 )
			return String::Printf( "Bone '%hs' (%d vertices)", boneName.AsChar(), m_vertexCount );
		else
			return "Influences";
	}

	THandle< ISerializable > MeshNode_BoneInfluences::GetEditableObject()
	{
		return CreateHandle< MeshBoneSetup >( GetTree().GetMesh(), m_boneIndex );
	}

	void MeshNode_BoneInfluences::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->SetMeshVisibility( false );
		vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_BoneInfluence >( GetTree().GetMesh(), m_boneIndex, m_chunkIndex, m_lodIndex ) );
	}

	void MeshNode_BoneInfluences::SetupInterop( MeshNode_BoneInfluences* lodsNode, ::interop::utils::Object& op )
	{
	}

	//-------

	MeshNode_MeshParameters::MeshNode_MeshParameters( StructureTree& parentTree )
		: IMeshStructureNode( parentTree )
	{

	}

	void MeshNode_MeshParameters::CreateNodes()
	{
		StructureTree& tree = GetTree();
		const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
		MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
		THandle< CMesh > mesh = Cast< CMesh >( sharedData->GetEditor().GetResource() );
		
		if ( const auto& shapePoses = mesh->GetParameter<mesh::MeshParamDeformableShapesData>() )
		{
			auto meshDeformable = red::CreateSharedPtr< MeshNode_MeshParamDeformableShapesData >(GetTree(), mesh );
			GetTree().AddNode( meshDeformable, *this );
		}

		if ( const auto& waterPatchData = mesh->GetParameter<mesh::MeshParamWaterPatchData>() )
		{
			auto waterPatch = red::CreateSharedPtr< MeshNode_MeshParamWaterPatchData >(GetTree(), mesh);
			GetTree().AddNode( waterPatch, *this );
		}
		
	}

	String MeshNode_MeshParameters::GetName() const
	{
		return "Mesh Parameters";
	}

	THandle<ISerializable> MeshNode_MeshParameters::GetEditableObject()
	{
		return nullptr;
	}

	void MeshNode_MeshParameters::SetupInterop( MeshNode_MeshParameters* paramNode, ::interop::utils::Object& op )
	{
		op.RegisterFunctor< void >( "CreateMeshParameterDeformableShapesData", [ paramNode ]( interop::IContext& ctx ) {
			StructureTree& tree = paramNode->GetTree();
			const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
			MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
			THandle< CMesh > mesh = Cast< CMesh >( sharedData->GetEditor().GetResource() );
			if( !mesh->GetParameter<mesh::MeshParamDeformableShapesData>() )
			{
				mesh->CreateParameter<mesh::MeshParamDeformableShapesData>();
				auto meshDeformable = red::CreateSharedPtr< MeshNode_MeshParamDeformableShapesData >(tree, mesh);
				tree.AddNode( meshDeformable, *paramNode );
			}
		} );
	}


	MeshNode_MeshParamWaterPatchData::MeshNode_MeshParamWaterPatchData( StructureTree& parentTree, THandle<CMesh>& mesh )
		: IMeshStructureNode( parentTree )
		, m_mesh( mesh )
	{
	}

	String MeshNode_MeshParamWaterPatchData::GetName() const
	{
		return "Mesh Parameter - Water Patch Data";
	}

	THandle< ISerializable > MeshNode_MeshParamWaterPatchData::GetEditableObject()
	{
		return m_mesh->GetParameter<mesh::MeshParamWaterPatchData>();
	}

	void MeshNode_MeshParamWaterPatchData::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
	}

	void MeshNode_MeshParamWaterPatchData::SetupInterop( MeshNode_MeshParamWaterPatchData* obj, ::interop::utils::Object& op )
	{
		op.RegisterFunctor< void >( "Remove", [obj]( interop::IContext& ctx )
		{
			THandle<mesh::MeshParamWaterPatchData>& waterPatchData = obj->m_mesh->GetParameter<mesh::MeshParamWaterPatchData>();
			if ( waterPatchData )
			{
				obj->m_mesh->RemoveParameter( waterPatchData ); // TODO: put into action so it's UNDOable
				StructureTree& tree = obj->GetTree();
				tree.RemoveNode( obj );
			}
		} );
	}


	//-------

	MeshNode_MeshParamDeformableShapesData::MeshNode_MeshParamDeformableShapesData( StructureTree& parentTree, THandle<CMesh>& mesh )
		: IMeshStructureNode( parentTree )
		, m_mesh( mesh )
	{
	}

	String MeshNode_MeshParamDeformableShapesData::GetName() const
	{
		return "Mesh Parameter - Deformable Shapes Data";
	}

	THandle< ISerializable > MeshNode_MeshParamDeformableShapesData::GetEditableObject()
	{
		return m_mesh->GetParameter<mesh::MeshParamDeformableShapesData>();
	}

	void MeshNode_MeshParamDeformableShapesData::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
	}

	void MeshNode_MeshParamDeformableShapesData::SetupInterop( MeshNode_MeshParamDeformableShapesData* obj, ::interop::utils::Object& op )
	{
		op.RegisterFunctor< void >( "Remove", [ obj ]( interop::IContext& ctx ) 
		{
			THandle<mesh::MeshParamDeformableShapesData>& shapePoses = obj->m_mesh->GetParameter<mesh::MeshParamDeformableShapesData>();
			if( shapePoses )
			{
				obj->m_mesh->RemoveParameter( shapePoses ); // TODO: put into action so it's UNDOable
				StructureTree& tree = obj->GetTree();
				tree.RemoveNode( obj );
			}
		} );

		op.RegisterFunctor< void >( "SaveInitialVehiclePoses", [ obj ]( interop::IContext& ctx ) 
		{
			StructureTree& tree = obj->GetTree();
			const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
			MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
			sharedData->SaveInitialVehiclePoses();
		} );

		op.RegisterFunctor< void >( "SaveFinalVehiclePoses", [ obj ]( interop::IContext& ctx ) 
		{
			StructureTree& tree = obj->GetTree();
			const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
			MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
			sharedData->SaveFinalVehiclePoses();
		} );

		op.RegisterFunctor< void >( "SwitchToInitialVehiclePoses", [ obj ]( interop::IContext& ctx ) 
		{
			StructureTree& tree = obj->GetTree();
			const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
			MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
			sharedData->SwitchToInitialVehiclePoses();
		} );

		op.RegisterFunctor< void >( "SwitchToFinalVehiclePoses", [ obj ]( interop::IContext& ctx ) 
		{
			StructureTree& tree = obj->GetTree();
			const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
			MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
			sharedData->SwitchToFinalVehiclePoses();
		} );
	}


	//-------

	IMeshPhysicsStructureNode::IMeshPhysicsStructureNode( StructureTree& parentTree )
		: IMeshStructureNode( parentTree )
	{}

	void IMeshPhysicsStructureNode::OnPhysicsObjectSelected( const THandle< physics::PhysicsBase >& object )
	{
		if( GetEditableObject() == object )
		{
			red::Set< TreeNodeID > visitedNodes = {red::PoolBackend()};
			TTreeNodeIDs orderedNodesToExpand = {red::PoolBackend()};
			GetTree().ExpandParent( &GetTree(), GetId(), visitedNodes, orderedNodesToExpand );
			GetTree().NotifyNodesExpanded( orderedNodesToExpand );
			GetTree().SelectNodeFromPreviewSelection( { GetId() } );
		}
	}

	void IMeshPhysicsStructureNode::SetupInterop( IMeshPhysicsStructureNode* physicsSystemNode, ::interop::utils::Object& op )
	{}

	//-------

	MeshNode_PhysicsSystem::MeshNode_PhysicsSystem( StructureTree& parentTree, const THandle< physics::PhysicsEditorScene >& editorScene, const String& name )
		: IMeshPhysicsStructureNode( parentTree )
		, m_editorScene( editorScene )
		, m_name( name )
	{
		RED_ASSERT( m_editorScene, "Trying to create physics system structure tree node without editor scene available." );
	}

	MeshNode_PhysicsSystem::~MeshNode_PhysicsSystem()
	{
	}

	struct PhysicsActorHolder
	{
		physics::PhysicsActorPtr m_actor;
		red::SharedPtr< MeshNode_PhysicsSystemBody > m_node;
		Bool m_isInHierarchy;
	};

	void MeshNode_PhysicsSystem::CreateNodes()
	{
		auto physicsObjects = m_editorScene->GetPhysicsObjectsOnScene();

		red::HashMap< physics::PhysicsBaseID, PhysicsActorHolder > actors = {red::PoolBackend()};
		red::DynArray< physics::PhysicsJointPtr > joints = {red::PoolBackend()};

		for( physics::PhysicsBasePtr object : physicsObjects )
		{
			if( object->GetBaseType() == physics::EPBT_Actor )
			{
				physics::PhysicsActorPtr actorPtr = Cast< physics::PhysicsActor >( object );
				auto actorNodePtr = red::CreateSharedPtr< MeshNode_PhysicsSystemBody >( GetTree(), this, actorPtr );
				actorNodePtr->RegisterForNotifications();
				actors.Insert( actorPtr->GetPhysicsObjectID(), {actorPtr, actorNodePtr, false} );
			}
			else if( object->GetBaseType() == physics::EPBT_Joint )
			{
				physics::PhysicsJointPtr jointPtr = Cast< physics::PhysicsJoint >( object );
				joints.PushBack( jointPtr );
			}
		}

		for( auto jointPtr : joints )
		{
			auto jointNode = red::CreateSharedPtr< MeshNode_PhysicsSystemJoint >( GetTree(), this, jointPtr );
			jointNode->RegisterForNotifications();
			physics::PhysicsBaseID parentId = jointPtr->GetActorId( physics::EPJAI_Parent );
			physics::PhysicsBaseID childId = jointPtr->GetActorId( physics::EPJAI_Child );
			Bool parentInStructure = actors.KeyExist( parentId );
			Bool childInStructure = actors.KeyExist( childId );
			if( parentInStructure && childInStructure )
			{
				PhysicsActorHolder& actorData = actors[ parentId ];
				GetTree().AddNode( jointNode, *actorData.m_node );
				if( !actors[ childId ].m_isInHierarchy )
				{
					GetTree().AddNode( actors[ childId ].m_node, *jointNode );
					actors[ childId ].m_isInHierarchy = true;
				}
			}
			else
			{
				if( parentInStructure )
				{
					if( !actors[ parentId ].m_isInHierarchy )
					{
						GetTree().AddNode( actors[ parentId ].m_node, *this );
						actors[ parentId ].m_isInHierarchy = true;
					}
					GetTree().AddNode( jointNode, *actors[ parentId ].m_node );
				}
				else if( childInStructure )
				{
					GetTree().AddNode( jointNode, *this );
					if( actors[ childId ].m_isInHierarchy )
					{
						auto jointNodePtr = red::CreateSharedPtr< MeshNode_PhysicsSystemBody >( GetTree(), this, actors[ childId ].m_actor );
						jointNodePtr->RegisterForNotifications();
						GetTree().AddNode( jointNodePtr, *jointNode );
					}
					else
					{
						GetTree().AddNode( actors[ childId ].m_node, *jointNode );
						actors[ childId ].m_isInHierarchy = true;
					}
				}
				else
				{
					GetTree().AddNode( jointNode, *this );
				}
			}
		}

		auto actorsIterator = actors.Begin();
		while( actorsIterator != actors.End() )
		{
			PhysicsActorHolder& actorData = actorsIterator.Value();
			if( !actorData.m_isInHierarchy && actorData.m_node )
			{
				GetTree().AddNode( actorData.m_node, *this );
			}
			++actorsIterator;
		}
	}

	String MeshNode_PhysicsSystem::GetName() const
	{
		String name = m_name;
		Uint32 numBodies = 0;
		Uint32 numJoints = 0;
		Uint32 numShapes = 0;

		const red::DynArray< physics::PhysicsBasePtr >& objects = m_editorScene->GetPhysicsObjectsOnScene();
		for( const auto& object : objects )
		{
			if( auto body = Cast< physics::PhysicsActor >( object ) )
			{
				++numBodies;
				numShapes += body->GetActorShapes().Size();
			}
			else if( auto joint = Cast< physics::PhysicsJoint >( object ) )
			{
				++numJoints;
			}
		}

		if( numBodies > 0 )
			name += String::Printf( ", %u bodies ", numBodies );

		if( numJoints > 0 )
			name += String::Printf( ", %u joints ", numJoints );

		if( numShapes > 0 )
			name += String::Printf( ", %u shapes ", numShapes );


		return name;
	}

	THandle< ISerializable > MeshNode_PhysicsSystem::GetEditableObject()
	{
		return m_editorScene;
	}

	void MeshNode_PhysicsSystem::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
	}

	const THandle< physics::PhysicsEditorScene >& MeshNode_PhysicsSystem::GetEditorScene()
	{
		return m_editorScene;
	}

	void MeshNode_PhysicsSystem::OnPhysicsObjectAdded( const THandle< physics::PhysicsBase >& object )
	{
		RecreateNodes();

		// Fix node name
		TTreeNodeIDs ids = {red::PoolBackend()};
		ids.PushBack( GetId() );
		GetTree().InvalidateNodes_DEPRECATED( GetParentId(), ids );
	}

	void MeshNode_PhysicsSystem::OnPhysicsObjectRemoved()
	{
		RecreateNodes();
		// Fix node name
		TTreeNodeIDs ids = {red::PoolBackend()};
		ids.PushBack( GetId() );
		GetTree().InvalidateNodes_DEPRECATED( GetParentId(), ids );
	}

	void MeshNode_PhysicsSystem::RegisterForNotifications()
	{
		m_editorScene->AttachNotificationHandler( red::SharedFromPtr<IMeshPhysicsStructureNode>( this ) );
	}

	Bool MeshNode_PhysicsSystem::IsBeingEdited()
	{
		StructureTree& tree = GetTree();
		const red::DynArray< THandle< physics::PhysicsBase > >& objects = m_editorScene->GetPhysicsObjectsOnScene();
		const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
		MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
		if( sharedData->GetEditModeName() == m_editorScene->GetPhysicsObjectType() )
			return true;

		return false;
	}

	void MeshNode_PhysicsSystem::SetupInterop( MeshNode_PhysicsSystem* physicsSystemNode, ::interop::utils::Object& op )
	{

		op.RegisterFunctor< void >( "Remove", [physicsSystemNode]( interop::IContext& ctx ) {
			StructureTree& tree = physicsSystemNode->GetTree();
			const red::DynArray< THandle< physics::PhysicsBase > >& objects = physicsSystemNode->m_editorScene->GetPhysicsObjectsOnScene();
			const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
			MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
			red::DynArray< EditorObjectIDPath > paths{red::PoolBackend()};
			paths.Reserve( objects.Size() );
			for( const THandle< physics::PhysicsBase >& obj : objects )
			{
				if( obj->GetBaseType() != physics::EPhysicsBaseType::EPBT_Mesh )
				{
					paths.PushBack( EditorObjectIDPath( obj->GetEditorObjectID() ) );
				}
			}
			sharedData->DeleteObjects( paths );
		} );

		op.RegisterFunctor< void >( "ComputeCOM", [physicsSystemNode]( interop::IContext& ctx ) {
			const red::DynArray< THandle< physics::PhysicsBase > >& objects = physicsSystemNode->m_editorScene->GetPhysicsObjectsOnScene();
			StructureTree& tree = physicsSystemNode->GetTree();
			const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
			MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
			red::DynArray< EditorObjectID > objectIds{red::PoolBackend()};
			objectIds.Reserve( objects.Size() );
			for( const THandle< physics::PhysicsBase >& obj : objects )
			{
				if( THandle< physics::PhysicsActor > physActor = Cast< physics::PhysicsActor >( obj ) )
				{
					objectIds.PushBack( physActor->GetEditorObjectID() );
				}
			}
			sharedData->RecomputeCOM( objectIds );
		} );

		op.RegisterFunctor< void >( "GenerateDestructionHierarchy", [physicsSystemNode]( interop::IContext& ctx ) {
			StructureTree& tree = physicsSystemNode->GetTree();
			const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
			MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
			sharedData->GenerateDefaultDestructionHierarchy();
		} );

		op.RegisterValueRO< String >( "sceneType",
									  [physicsSystemNode]() {
										  if( THandle< physics::PhysicsEditorScene > scene = physicsSystemNode->GetEditorScene() )
										  {
											  return scene->GetPhysicsObjectType().AsStringView().ToString();
										  }
										  return String::EMPTY();
									  } );

		op.RegisterValueRO< Bool >( "isBeingEdited",
									[physicsSystemNode]() {
										return physicsSystemNode->IsBeingEdited();
									} );
	}

	//-------

	MeshNode_PhysicsSystemBody::MeshNode_PhysicsSystemBody( StructureTree& parentTree, MeshNode_PhysicsSystem* system, physics::PhysicsActorPtr body )
		: IMeshPhysicsStructureNode( parentTree )
		, m_system( system )
		, m_actor( body )
	{
		RED_ASSERT( m_actor, "Trying to create system body structure tree node with nullptr actor." );
	}

	MeshNode_PhysicsSystemBody::~MeshNode_PhysicsSystemBody()
	{
	}

	void MeshNode_PhysicsSystemBody::CreateNodes()
	{
		GetTree().AddNode( red::CreateSharedPtr< MeshNode_PhysicsActorCOM >( GetTree(), m_system, m_actor->GetCOM() ), *this );

		for( auto shape : m_actor->GetActorShapes() )
		{
			auto sharedPtr = red::CreateSharedPtr< MeshNode_PhysicsSystemShape >( GetTree(), m_system, m_actor, shape );
			sharedPtr->RegisterForNotifications();
			GetTree().AddNode( sharedPtr, *this );
		}
	}

	String MeshNode_PhysicsSystemBody::GetName() const
	{
		String name = m_actor->GetName();
		return name.AsChar();
	}

	THandle< ISerializable > MeshNode_PhysicsSystemBody::GetEditableObject()
	{
		return m_actor;
	}


	void MeshNode_PhysicsSystemBody::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{}

	Bool MeshNode_PhysicsSystemBody::OnDragAndDrop( ITreeNode* target )
	{
		IMeshStructureNode* node = (IMeshStructureNode*)target;
		if( node != nullptr )
		{
			auto data = node->GetEditableObject();
			if( data )
			{
				if( THandle< physics::PhysicsActor > physActor = Cast< physics::PhysicsActor >( data ) )
				{
					if( physActor != m_actor )
					{
						physics::PhysicsMeshBoneMapping newBoneMap = physActor->GetBoneMap();
						physics::PhysicsMeshBoneMapping currentBoneMap = m_actor->GetBoneMap();

						m_system->GetEditorScene()->BindPhysicsActorToMesh( physActor->GetPhysicsObjectID(), currentBoneMap.m_meshId, currentBoneMap.m_boneName );
						m_system->GetEditorScene()->BindPhysicsActorToMesh( m_actor->GetPhysicsObjectID(), newBoneMap.m_meshId, newBoneMap.m_boneName );
						physActor->SnapToBone();
						m_actor->SnapToBone();

						return true;
					}
				}
				else if( THandle< physics::PhysicsMesh > physMesh = Cast< physics::PhysicsMesh >( data ) )
				{
					m_system->GetEditorScene()->BindPhysicsActorToMesh( m_actor->GetPhysicsObjectID(), physMesh->GetPhysicsObjectID(), CName::NONE() );
					return true;
				}
				else if( THandle< physics::PhysicsEditorScene > editorScene = Cast< physics::PhysicsEditorScene >( data ) )
				{
					editorScene->DestroyPhysicsActorToMeshBinding( m_actor->GetPhysicsObjectID() );
					return true;
				}
			}
		}

		return false;
	}

	const physics::PhysicsActorPtr& MeshNode_PhysicsSystemBody::GetActor()
	{
		return m_actor;
	}

	void MeshNode_PhysicsSystemBody::OnPhysicsObjectAdded( const THandle< physics::PhysicsBase >& object )
	{
		if( THandle< physics::PhysicsShape > shape = Cast< physics::PhysicsShape >( object ) )
		{
			auto shapeSharedPtr = red::CreateSharedPtr< MeshNode_PhysicsSystemShape >( GetTree(), m_system, m_actor, shape );
			shapeSharedPtr->RegisterForNotifications();
			GetTree().AddNode( shapeSharedPtr, *this );
		}
	}

	void MeshNode_PhysicsSystemBody::OnPhysicsObjectPropertyChanged( const THandle< physics::PhysicsBase >& object, const rtti::AccessPath& path )
	{
		if( path.ToString() == "actorCOM" )
		{
			// create a node if we don't have one
			auto children = GetChildren();
			for( const auto& child : children )
			{
				IMeshStructureNode* node = static_cast< IMeshStructureNode* >( child.Get() );
				auto data = node->GetEditableObject();
				if( const THandle< physics::EditorActorCOM >& actorCom = Cast< physics::EditorActorCOM >( data ) )
				{
					MeshNode_PhysicsActorCOM* actorComNode = (MeshNode_PhysicsActorCOM*)node;
					actorComNode->SetActorCOM( m_actor->GetCOM() );
					actorComNode->RefreshNodeCaption();
					RefreshNodeCaption();
					return;
				}
			}
			GetTree().AddNode( red::CreateSharedPtr< MeshNode_PhysicsActorCOM >( GetTree(), m_system, m_actor->GetCOM() ), *this );
		}
		else
		{
			TTreeNodeIDs ids = {red::PoolBackend()};
			ids.PushBack( GetId() );
			GetTree().InvalidateNodes_DEPRECATED( GetParentId(), ids );
		}
	}

	void MeshNode_PhysicsSystemBody::RegisterForNotifications()
	{
		m_actor->AttachNotificationHandler( red::SharedFromPtr<IMeshPhysicsStructureNode>( this ) );
	}

	void MeshNode_PhysicsSystemBody::SetupInterop( MeshNode_PhysicsSystemBody* sbNode, ::interop::utils::Object& op )
	{
		op.RegisterValueRO< Bool >( "isBeingEdited",
									[sbNode]() {
										return sbNode->m_system->IsBeingEdited();
									} );

		op.RegisterValueRO< Uint8 >( "actorType",
									 [sbNode]() {
										 if( const physics::PhysicsActorPtr& actor = sbNode->GetActor() )
										 {
											 return actor->GetSimulationType();
										 }
										 return physics::EPhysicsSimulation::EPS_Undefined;
									 } );

		op.RegisterFunctor< void >( "Remove", [sbNode]( interop::IContext& ctx ) {
			if( const physics::PhysicsActorPtr& actor = sbNode->GetActor() )
			{
				StructureTree& tree = sbNode->GetTree();
				const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
				MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
				red::DynArray< EditorObjectIDPath > paths{red::PoolBackend()};
				paths.PushBack( EditorObjectIDPath( actor->GetEditorObjectID() ) );
				sharedData->DeleteObjects( paths );
			}
		} );

		op.RegisterFunctor< void >( "ComputeCOM", [sbNode]( interop::IContext& ctx ) {
			if( const physics::PhysicsActorPtr& actor = sbNode->GetActor() )
			{
				StructureTree& tree = sbNode->GetTree();
				const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
				MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
				red::DynArray< EditorObjectID > objectIds{red::PoolBackend()};
				objectIds.PushBack( actor->GetEditorObjectID() );
				sharedData->RecomputeCOM( objectIds );
			}
		} );

		op.RegisterFunctor< void >( "SnapToParent", [sbNode]( interop::IContext& ctx ) {
			if( const physics::PhysicsActorPtr& actor = sbNode->GetActor() )
			{
				StructureTree& tree = sbNode->GetTree();
				const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
				MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
				red::DynArray< EditorObjectID > objectIds{red::PoolBackend()};
				objectIds.PushBack( actor->GetEditorObjectID() );
				sharedData->SnapToParent( objectIds );
			}
		} );

		op.RegisterFunctor< void >( "SnapToParentAlternativeMode", [sbNode]( interop::IContext& ctx ) {
			if( const physics::PhysicsActorPtr& actor = sbNode->GetActor() )
			{
				StructureTree& tree = sbNode->GetTree();
				const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
				MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
				red::DynArray< EditorObjectID > objectIds{red::PoolBackend()};
				objectIds.PushBack( actor->GetEditorObjectID() );
				sharedData->SnapToParentAlternativeMode( objectIds );
			}
		} );
	}

	//-------

	MeshNode_PhysicsSystemJoint::MeshNode_PhysicsSystemJoint( StructureTree& parentTree, MeshNode_PhysicsSystem* system, physics::PhysicsJointPtr joint )
		: IMeshPhysicsStructureNode( parentTree )
		, m_system( system )
		, m_joint( joint )
	{
		RED_ASSERT( m_joint, "Trying to create system joint structure tree node with nullptr joint." );
	}

	MeshNode_PhysicsSystemJoint::~MeshNode_PhysicsSystemJoint()
	{}

	void MeshNode_PhysicsSystemJoint::CreateNodes()
	{
	}

	String MeshNode_PhysicsSystemJoint::GetName() const
	{
		return m_joint->GetName();
	}

	THandle< ISerializable > MeshNode_PhysicsSystemJoint::GetEditableObject()
	{
		return m_joint;
	}

	void MeshNode_PhysicsSystemJoint::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
	}

	const physics::PhysicsJointPtr& MeshNode_PhysicsSystemJoint::GetJoint()
	{
		return m_joint;
	}

	void MeshNode_PhysicsSystemJoint::OnPhysicsObjectPropertyChanged( const THandle< physics::PhysicsBase >& object, const rtti::AccessPath& path )
	{
		CName propName;
		if( rtti::PathParser( path ).EatName( propName ) )
		{
			if( propName == RED_NAME_CONSTEXPR_NOREG( "actors" ) )
			{
				m_system->RecreateNodes();
			}
		}
	}

	void MeshNode_PhysicsSystemJoint::RegisterForNotifications()
	{
		m_joint->AttachNotificationHandler( red::SharedFromPtr<IMeshPhysicsStructureNode>( this ) );
	}

	void MeshNode_PhysicsSystemJoint::SetupInterop( MeshNode_PhysicsSystemJoint* sjNode, ::interop::utils::Object& op )
	{
		op.RegisterValueRO< Bool >( "isBeingEdited",
									[sjNode]() {
										return sjNode->m_system->IsBeingEdited();
									} );

		op.RegisterFunctor< void >( "Remove", [sjNode]( interop::IContext& ctx ) {
			if( const physics::PhysicsJointPtr& joint = sjNode->GetJoint() )
			{
				StructureTree& tree = sjNode->GetTree();
				const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
				MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
				red::DynArray< EditorObjectIDPath > paths{red::PoolBackend()};
				paths.PushBack( EditorObjectIDPath( joint->GetEditorObjectID() ) );
				sharedData->DeleteObjects( paths );
			}
		} );

		op.RegisterFunctor< void >( "SnapToParent", [sjNode]( interop::IContext& ctx ) {
			if( const physics::PhysicsJointPtr& joint = sjNode->GetJoint() )
			{
				StructureTree& tree = sjNode->GetTree();
				const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
				MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
				red::DynArray< EditorObjectID > objectIds{red::PoolBackend()};
				objectIds.PushBack( joint->GetEditorObjectID() );
				sharedData->SnapToParent( objectIds );
			}
		} );

		op.RegisterFunctor< void >( "SnapToParentAlternativeMode", [sjNode]( interop::IContext& ctx ) {
			if( const physics::PhysicsJointPtr& joint = sjNode->GetJoint() )
			{
				StructureTree& tree = sjNode->GetTree();
				const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
				MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
				red::DynArray< EditorObjectID > objectIds{red::PoolBackend()};
				objectIds.PushBack( joint->GetEditorObjectID() );
				sharedData->SnapToParentAlternativeMode( objectIds );
			}
		} );
	}

	//-------

	MeshNode_PhysicsSystemMesh::MeshNode_PhysicsSystemMesh( StructureTree& parentTree, physics::PhysicsMeshPtr mesh )
		: IMeshPhysicsStructureNode( parentTree )
		, m_mesh( mesh )
	{
		RED_ASSERT( m_mesh, "Trying to create physics mesh structure tree node with nullptr mesh." );
	}

	MeshNode_PhysicsSystemMesh::~MeshNode_PhysicsSystemMesh()
	{}

	void MeshNode_PhysicsSystemMesh::CreateNodes()
	{
	}

	String MeshNode_PhysicsSystemMesh::GetName() const
	{
		return m_mesh->GetName();
	}

	THandle< ISerializable > MeshNode_PhysicsSystemMesh::GetEditableObject()
	{
		return m_mesh;
	}

	void MeshNode_PhysicsSystemMesh::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
	}

	Bool MeshNode_PhysicsSystemMesh::OnDragAndDrop( ITreeNode* target )
	{
		return false;
	}

	const physics::PhysicsMeshPtr& MeshNode_PhysicsSystemMesh::GetMesh()
	{
		return m_mesh;
	}

	void MeshNode_PhysicsSystemMesh::RegisterForNotifications()
	{
		m_mesh->AttachNotificationHandler( red::SharedFromPtr<IMeshPhysicsStructureNode>( this ) );
	}

	void MeshNode_PhysicsSystemMesh::SetupInterop( MeshNode_PhysicsSystemMesh* meshNode, ::interop::utils::Object& op )
	{
		op.RegisterValueRO< Bool >( "isBeingEdited",
									[meshNode]() {
										StructureTree& tree = meshNode->GetTree();
										const auto& editorId = meshNode->GetEditableObject()->GetEditorObjectID();
										const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
										MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
										if( sharedData->GetEditModeName() == editorId.GetKind() )
											return true;

										return false;
									} );

		op.RegisterFunctor< void >( "Remove", [meshNode]( interop::IContext& ctx ) {
			if( const physics::PhysicsMeshPtr& mesh = meshNode->GetMesh() )
			{
				StructureTree& tree = meshNode->GetTree();
				const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
				MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
				red::DynArray< EditorObjectIDPath > paths{red::PoolBackend()};
				paths.PushBack( EditorObjectIDPath( mesh->GetEditorObjectID() ) );
				sharedData->DeleteObjects( paths );
			}
		} );
	}

	//-------

	MeshNode_PhysicsSystemShape::MeshNode_PhysicsSystemShape( StructureTree& parentTree, MeshNode_PhysicsSystem* system, physics::PhysicsActorPtr body, physics::PhysicsShapePtr shape )
		: IMeshPhysicsStructureNode( parentTree )
		, m_system( system )
		, m_actor( body )
		, m_shape( shape )
	{
		RED_ASSERT( m_shape, "Trying to create physics mesh structure tree node with nullptr shape." );
	}

	MeshNode_PhysicsSystemShape::~MeshNode_PhysicsSystemShape()
	{
	}

	String MeshNode_PhysicsSystemShape::GetName() const
	{
		String name = m_shape->GetName();
		return name.AsChar();
	}

	THandle< ISerializable > MeshNode_PhysicsSystemShape::GetEditableObject()
	{
		return m_shape;
	}

	void MeshNode_PhysicsSystemShape::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{}

	Bool MeshNode_PhysicsSystemShape::OnDragAndDrop( ITreeNode* target )
	{
		IMeshStructureNode* node = (IMeshStructureNode*)target;
		if( node != nullptr )
		{
			auto data = node->GetEditableObject();
			if( data )
			{
				if( THandle< physics::PhysicsActor > physActor = Cast< physics::PhysicsActor >( data ) )
				{
					auto editorScene = m_system->GetEditorScene();
					auto currentParent = m_shape->GetActorParent();
					if( physActor != currentParent )
					{
						currentParent->RemoveShape( m_shape );
						m_shape->SetFlag( physics::EPBF_RequestRenderingRebuild, true );
						physActor->AddShape( m_shape );
						m_shape->SetParentId( physActor->GetPhysicsObjectID() );
						return true;
					}
				}
			}
		}
		return false;
	}

	const physics::PhysicsShapePtr& MeshNode_PhysicsSystemShape::GetShape()
	{
		return m_shape;
	}

	void MeshNode_PhysicsSystemShape::RegisterForNotifications()
	{
		m_shape->AttachNotificationHandler( red::SharedFromPtr<IMeshPhysicsStructureNode>( this ) );
	}

	void MeshNode_PhysicsSystemShape::SetupInterop( MeshNode_PhysicsSystemShape* sbNode, ::interop::utils::Object& op )
	{
		op.RegisterValueRO< Bool >( "isBeingEdited",
									[sbNode]() {
										return sbNode->m_system->IsBeingEdited();
									} );

		op.RegisterValueRO< Bool >( "isImported",
									[sbNode]() {
										if( const physics::PhysicsShapePtr& shape = sbNode->GetShape() )
										{
											return shape->IsImported();
										}
										return false;
									} );

		op.RegisterValueRO< Uint8 >( "shapeType",
									 [sbNode]() {
										 if( const physics::PhysicsShapePtr& shape = sbNode->GetShape() )
										 {
											 if( auto shapeGeom = shape->GetShapeGeometry() )
											 {
												 return shapeGeom->m_shapeType;
											 }
										 }
										 return physics::EPhysicsShapeGeometryType::EPST_Invalid;
									 } );

		op.RegisterFunctor< void >( "Remove", [sbNode]( interop::IContext& ctx ) {
			if( const physics::PhysicsShapePtr& shape = sbNode->GetShape() )
			{
				StructureTree& tree = sbNode->GetTree();
				const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
				MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
				red::DynArray< EditorObjectIDPath > paths{red::PoolBackend()};
				paths.PushBack( EditorObjectIDPath( shape->GetEditorObjectID() ) );
				sharedData->DeleteObjects( paths );
			}
		} );

		op.RegisterFunctor< void >( "SnapToParent", [sbNode]( interop::IContext& ctx ) {
			if( const physics::PhysicsShapePtr& shape = sbNode->GetShape() )
			{
				StructureTree& tree = sbNode->GetTree();
				const MeshResourceEditor* editorFallback = tree.GetMeshResourceEditor();
				MeshPanelSharedData* sharedData = editorFallback->GetMeshPanelSharedData();
				red::DynArray< EditorObjectID > objectIds{red::PoolBackend()};
				objectIds.PushBack( shape->GetEditorObjectID() );
				sharedData->SnapToParent( objectIds );
			}
		} );
	}

	MeshNode_PhysicsActorCOM::MeshNode_PhysicsActorCOM( StructureTree& parentTree, MeshNode_PhysicsSystem* system, THandle< physics::EditorActorCOM > m_actorCOM )
		: IMeshPhysicsStructureNode( parentTree )
		, m_system( system )
		, m_actorCOM( m_actorCOM )
	{}

	void MeshNode_PhysicsSystemShape::CreateNodes()
	{}

	void MeshNode_PhysicsActorCOM::SetupInterop( MeshNode_PhysicsActorCOM* sbNode, ::interop::utils::Object& op )
	{}

	void MeshNode_PhysicsActorCOM::CreateNodes()
	{}

	String MeshNode_PhysicsActorCOM::GetName() const
	{
		return m_actorCOM->GetName();
	}

	THandle< ISerializable > MeshNode_PhysicsActorCOM::GetEditableObject()
	{
		return m_actorCOM;
	}

	void MeshNode_PhysicsActorCOM::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{}

	void MeshNode_PhysicsActorCOM::SetActorCOM( const THandle< physics::EditorActorCOM >& actorCOM )
	{
		m_actorCOM = actorCOM;
	}

	MeshNode_Cloth::MeshNode_Cloth( StructureTree& parentTree )
		: TBaseClass(parentTree)
	{}

	THandle< ISerializable > MeshNode_Cloth::GetEditableObject()
	{
		return nullptr;
	}

	void MeshNode_Cloth::SetupVisualization(IMeshVisualizationContainer* vis) const
	{}

	void MeshNode_Cloth::SetupInterop(MeshNode_Cloth* ptr, interop::utils::Object& op)
	{}

	void MeshNode_Cloth::CreateNodes()
	{
		auto& tree = GetTree();
		auto view = tree.GetMeshView();
		if( view.GetParameter<mesh::MeshParamCloth_Graphical>() )
		{
			auto node = red::CreateSharedPtr< MeshNode_GraphicalCloth >( tree );
			tree.AddNode( node, *this );
		}

		if( view.GetParameter<mesh::MeshParamCloth>() )
		{
			auto node = red::CreateSharedPtr< MeshNode_PhysicalCloth >( tree );
			tree.AddNode( node, *this );
		}
	}

	red::String MeshNode_Cloth::GetName() const
	{
		return "Cloth";
	}

	MeshNode_GraphicalCloth::MeshNode_GraphicalCloth(StructureTree& parentTree)
		: TBaseClass(parentTree)
	{}

	THandle< ISerializable > MeshNode_GraphicalCloth::GetEditableObject()
	{
		return nullptr;
	}

	void MeshNode_GraphicalCloth::SetupVisualization(IMeshVisualizationContainer* vis) const
	{}

	void MeshNode_GraphicalCloth::CreateNodes()
	{}

	red::String MeshNode_GraphicalCloth::GetName() const
	{
		return "Graphical Cloth Data";
	}

	void MeshNode_GraphicalCloth::SetupInterop( MeshNode_GraphicalCloth* ptr, ::interop::utils::Object& op )
	{
		op.RegisterFunctor<void>("Remove", [ptr]( interop::IContext& ctx ) 
		{
			auto param = ptr->GetTree().GetMesh()->GetParameter<mesh::MeshParamCloth_Graphical>();
			ptr->GetTree().GetMeshResourceEditor()->GetMeshPanelSharedData()->RemoveClothParam(param);
			ptr->RecreateNodes();
		});
	}

	MeshNode_PhysicalCloth::MeshNode_PhysicalCloth(StructureTree& parentTree)
		: TBaseClass(parentTree)
	{}

	THandle< ISerializable > MeshNode_PhysicalCloth::GetEditableObject()
	{
		return nullptr;
	}

	void MeshNode_PhysicalCloth::SetupVisualization(IMeshVisualizationContainer* vis) const
	{}

	void MeshNode_PhysicalCloth::CreateNodes()
	{}

	red::String MeshNode_PhysicalCloth::GetName() const
	{
		return "Physical Cloth Data";
	}

	void MeshNode_PhysicalCloth::SetupInterop( MeshNode_PhysicalCloth* ptr, ::interop::utils::Object& op )
	{
		op.RegisterFunctor<void>("Remove", [ptr]( interop::IContext& ctx ) 
		{
			auto param = ptr->GetTree().GetMesh()->GetParameter<mesh::MeshParamCloth>();
			ptr->GetTree().GetMeshResourceEditor()->GetMeshPanelSharedData()->RemoveClothParam(param);
			ptr->RecreateNodes(1);
		});
	}

	//-------

	MeshNode_Occluder::MeshNode_Occluder( StructureTree& parentTree, THandle< mesh::MeshParamOccluderData > occluderData )
		: IMeshStructureNode( parentTree )
		, m_occluderData( occluderData )
	{
	}

	void MeshNode_Occluder::SetupInterop( MeshNode_Occluder* lodsNode, ::interop::utils::Object& op )
	{
	}

	void MeshNode_Occluder::CreateNodes()
	{
	}

	String MeshNode_Occluder::GetName() const
	{
		return "Occlusion";
	}

	THandle< ISerializable > MeshNode_Occluder::GetEditableObject()
	{
		return m_occluderData;
	}

	void MeshNode_Occluder::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
	}

	MeshNode_WorkspotOffsets::MeshNode_WorkspotOffsets( StructureTree& parentTree, THandle< mesh::MeshParamWorkspotOffsets > offsets )
		: IMeshStructureNode( parentTree )
		, m_offsets( offsets )
	{
	}

	String MeshNode_WorkspotOffsets::GetName() const
	{
		return "Workspot slots";
	}

	THandle< ISerializable > MeshNode_WorkspotOffsets::GetEditableObject()
	{
		return nullptr;
	}

	void MeshNode_WorkspotOffsets::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_Workspots >( m_offsets ) );
	}

	void MeshNode_WorkspotOffsets::SetupInterop( MeshNode_WorkspotOffsets* workspotsNode, ::interop::utils::Object& op )
	{
	}

	MeshNode_ImportedSnapPoints::MeshNode_ImportedSnapPoints( StructureTree& parentTree, THandle< mesh::MeshParamImportedSnapPoint > snapPoints )
		: IMeshStructureNode( parentTree )
		, m_snapPoints( snapPoints )
	{
	}

	String MeshNode_ImportedSnapPoints::GetName() const
	{
		return "Snap points";
	}

	THandle< ISerializable > MeshNode_ImportedSnapPoints::GetEditableObject()
	{
		return nullptr;
	}

	void MeshNode_ImportedSnapPoints::SetupVisualization( IMeshVisualizationContainer* vis ) const
	{
		vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_SnapPoints >( m_snapPoints ) );
	}

	void MeshNode_ImportedSnapPoints::SetupInterop( MeshNode_ImportedSnapPoints* workspotsNode, ::interop::utils::Object& op )
	{
	}

} // tools

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshAppearanceSetup, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_name ).readOnly();
RTTI_PROPERTY( m_chunkMaterials ).editable().inlined().customInnerTypeEditor( "MeshAppearanceMaterialName" );
RTTI_PROPERTY( m_tags ).editable().resizable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshLodSetup, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_distance ).editable();
RTTI_PROPERTY( m_triangleCount ).readOnly();
RTTI_PROPERTY( m_vertexCount ).readOnly();
RTTI_PROPERTY( m_chunksCount ).readOnly();
RTTI_PROPERTY( m_materialsCount ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshChunkMaterialEntry, tools );
RTTI_PROPERTY( m_name ).editable().customEditor( "MeshChunkMaterial" );
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshChunkSetup, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_vertexType ).readOnly();
RTTI_PROPERTY( m_triangleCount ).readOnly();
RTTI_PROPERTY( m_vertexCount ).readOnly();
RTTI_PROPERTY( m_numBonesPerVertex ).readOnly();
RTTI_PROPERTY_CATEGORY( "Rendering" );
RTTI_PROPERTY( m_flags ).editable();
RTTI_PROPERTY_CATEGORY( "SourceData" );
RTTI_PROPERTY( m_sourceMeshUUID ).readOnly();
RTTI_PROPERTY( m_sourceMaterialUUID ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshBoneSetup, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_name ).readOnly();
RTTI_PROPERTY( m_rig ).readOnly();
RTTI_PROPERTY( m_epsilon ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshBoundsSetup, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_box ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( VertexBoneIndices, tools );
RTTI_PROPERTY( m_boneIndex0 ).readOnly();
RTTI_PROPERTY( m_boneIndex1 ).readOnly();
RTTI_PROPERTY( m_boneIndex2 ).readOnly();
RTTI_PROPERTY( m_boneIndex3 ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshStreamVec4, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_data ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshStreamVec3, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_data ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshStreamVec2, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_data ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshStreamFloat, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_data ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshStreamColor, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_data ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshStreamUint16, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_data ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshStreamUint32, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_data ).readOnly();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshStreamBoneIndices, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_data ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshStreamGarmentCaps, tools );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY( m_data ).readOnly();
RTTI_PROPERTY( m_numVerticesMarkedAsCaps ).readOnly();
RTTI_PROPERTY( m_numVerticesNonCaps ).readOnly();
RTTI_PROPERTY( m_numTrianglesMarkedAsCaps ).readOnly();
RTTI_PROPERTY( m_numTrianglesPartiallyMarkedAsCaps ).readOnly();
RTTI_PROPERTY( m_numTrianglesNonCaps ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CustomPropertyEditor_MeshChunkMaterial, tools );
RTTI_PARENT_TYPE( IEditableCustomProperty );
RTTI_END_TYPE();
