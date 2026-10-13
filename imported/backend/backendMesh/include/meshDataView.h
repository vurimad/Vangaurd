/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/resourceMesh/include/meshDataViewRO.h"

class CMesh;
class CDateTime;

namespace mesh
{
	struct BACKEND_MESH_API ChunkStreamData
	{
		EMeshStreamType m_stream;
		void* m_data;
		Uint32 m_stride;
		Uint32 m_count;

		serialization::BufferHandleRO m_buffer;

		RED_INLINE ChunkStreamData()
			: m_stream(MST_None)
			, m_data(nullptr)
			, m_stride(0)
			, m_count(0)
		{}

		RED_INLINE Bool Empty() const { return m_data == nullptr; }
	};

	// Mode of operation when removing LOD from mesh, determines what will be done to chunks used by removed LOD (and not used by any other LOD).
	enum class RemoveLODMode : Uint8
	{
		RemoveUnusedChunks,
		ClearUnusedChunks,
		KeppAllChunks
	};

	/// non-read only data view
	/// allows to modify mesh properties but not LOD/Chunk count
	/// NOTE: mesh is not recreated right away (that would be to costly) you need to wait until the DataView object is destroyed or the Apply() method is called
	class BACKEND_MESH_API DataView : public DataViewRO
	{
	public:
		DataView(CMesh& mesh);
		virtual ~DataView();

		/// Get non-const pointer to the edited mesh.
		RED_INLINE CMesh* GetMesh() { return m_mesh; }

		//---

		/// changes the distance when given LOD shows up
		/// NOTE: requires reuploading mesh to the renderer
		void SetLODDistance(LODIndex lodIndex, Float dist);

		Bool RemoveLODWithData( LODIndex lodIndex, RemoveLODMode mode = RemoveLODMode::RemoveUnusedChunks );
		Bool RemoveLOD( LODIndex lodIndex );

		//---

		/// changes chunk's vertex type - use with care, for experiments mostly
		/// NOTE: requires reuploading mesh to the renderer
		void SetChunkVertexType( ChunkIndex chunkIndex, EMeshVertexType newVertexType );

		/// changes in which LODs a given chunk is present
		/// NOTE: requires reuploading mesh to the renderer
		void SetChunkLodMask(ChunkIndex chunkIndex, Uint8 lodMask);

		/// changes material assignment on chunk
		/// NOTE: requires reuploading mesh to the renderer
		void SetChunkMaterialName(ChunkIndex chunkIndex, ChunkMaterialIndex index, CName materialName);

		/// changes general chunk flags
		/// NOTE: requires reuploading mesh to the renderer
		void SetChunkFlags(ChunkIndex chunkIndex, ChunkFlags flags);

		/// changes data of a chunk stream
		/// NOTE: requires rebuilding the blob and reuploading mesh to the renderer
		/// NOTE: the data count and format must match EXACTLY, we cannot extend the size of the mesh this way
		void SetChunkData(ChunkIndex chunkIndex, ChunkStreamDataRO data);

		void SetChunkSourceMeshUUID(ChunkIndex chunkIndex, const String& uuid);
		void SetChunkSourceMaterialUUID(ChunkIndex chunksIndex, const String& uuid);

		/// Remove the stream from the mesh chunk. We have an option to skip render blob recalculation. This is useful for cooking.
		void RemoveChunkStream( ChunkIndex chunkIndex, EMeshStreamType streamType, Bool recreateBlob = true );
		/// Remove multiple streams from the mesh chunk. We have an option to skip render blob recalculation. This is useful for cooking.
		void RemoveChunkStreams( ChunkIndex chunkIndex, Uint32 streamsMask = 0xFFFFFFFFu, Bool recreateBlob = true );
		/// Remove multiple streams from the mesh (all chunks). We have an option to skip render blob recalculation. This is useful for cooking.
		void RemoveMeshStreams( Uint32 streamsMask = 0xFFFFFFFFu, Bool recreateBlob = true );

		/// set material data for given material
		/// NOTE: requires reuploading the material setup
		void SetMaterialData(MaterialIndex index, THandle<IMaterial> materialData);

		/// set material name for given material
		void SetMaterialName(MaterialIndex index, CName name);

		//-----

		/// Remove unused bones from the mesh. Returns true if at least one bone was removed.
		/// Can return optional bone re-indexing table. This might be useful if we wanted to correct some additional bone indices after remove.
		/// Also is necessary for proper undo operation.
		/// This operation calls ReindexVertexBoneIndices() internally.
		Bool RemoveUnusedBones( red::DynArray<Uint32>& boneReindex );
		Bool RemoveUnusedBones();
		/// Reindex all vertex bone indices. This might be useful after bones were removed etc.
		void ReindexVertexBoneIndices( const red::DynArray<Uint32>& boneReindex );
		/// Invert bone re-index table.
		void InvertBoneReindex( red::DynArray<Uint32>& boneReindex );
		/// Reset all bone data. This operation is used by undo for remove unused bones.
		void ResetBones( const red::DynArray< CName >& newBonesNames,
		                 const red::DynArray< Matrix >& newBoneRigMatrices,
		                 const red::DynArray< Float >& newBoneVertexEpsilons );

		// Calculate vertex epsilons, used when creating bounding boxes for skinned meshes
		bool RecomputeVertexEpsilons();

		bool CreateSkinningLODs();

		//-----

		/// remove all materials from mesh
		void RemoveMaterials();

		/// Remove materials not used by any chunk in any appearance. 
		/// use previewInsteadOfRemove == true to just preview without actual removal
		void RemoveUnusedMaterials( red::DynArray<Uint32>* removedMaterialIndices = nullptr, Bool previewInsteadOfRemove = false );

		/// add new material, does not add it if already exists.
		/// When it already exists it will be either replaced with new version, or left untouched, base on overridePreviousVersion parameter.
		const Bool AddMaterial(THandle<IMaterial> materialData, CName name, Int32 index = -1, Bool overridePreviousVersion = true);

		/// remove material, does not remove material if its used
		const Bool RemoveMaterial(MaterialIndex index);

		/// Reset all materials, set new materials and new material names.
		/// The operation can fail if the new materials do not match with the appearances, like new materials
		/// do not contain some material used by appearances.
		/// It was added especially for the purpose of undo/redo of material list edit operations.
		Bool ResetMaterials( const red::DynArray< THandle< IMaterial > >& newMaterials, const red::DynArray< CName >& newMaterialNames );

		/// Generate a unique name for a material by appending suffix of the form "_X" where X is a number.
		String GenerateUniqueMaterialName( const String& initialMaterialName ) const;

		//-----

		/// remove all materials from chunk
		void RemoveChunkMaterials(const ChunkIndex chunkIndex);

		/// add material to chunk, material must exists
		const Bool AddChunkMaterial(const ChunkIndex chunkIndex, CName name);

		/// remove material from chunk
		const Bool RemoveChunkMaterial(const ChunkIndex chunkIndex, CName name);

		
		const Bool SetChunkMaterial( CName materialName, const ChunkIndex chunkIndex, const Uint32 appearanceIndex );
		//----

		/// add new appearance, does not add it if already exists
		const Bool AddAppearance( CName name, Int32 index = -1 );

		/// add existing appearance
		const Bool AddAppearance( THandle< mesh::MeshAppearance > appearance, Int32 index = -1 );
		/// add a copy of existing appearance
		const Bool AddAppearanceCopy( CName name, const mesh::MeshAppearance& sourceAppearance, Int32 index = -1 );

		/// remove appearance from mesh
		const Bool RemoveAppearance( CName name );

		/// remove all appearances from the mesh
		void RemoveAppearances();

		/// rename appearance from mesh
		const Bool RenameAppearance( Int32 index, CName newName );

		/// merges appearance to exisitng one or creates new
		const Bool ReplaceDefaultAppearance( THandle< mesh::MeshAppearance > appearance );

		/// make sure this appearance is valid to the mesh
		const Bool IsAppearanceCompatible( const mesh::MeshAppearance& apperance ) const;
		
		/// Set tags for given appearance.
		const Bool SetAppearanceTags( Int32 appearanceIndex, const red::DynArray< CName >& newTags );

		/// Set new material name in given appearance.
		const Bool SetAppearanceChunkMaterial( Int32 appearanceIndex, Int32 chunkIndex, CName materialName );

		//----

		/// Reimport multi-layer material, load all layers from JSON file.
		Bool ReimportLayersFromJSON( CName appearanceName, ChunkIndex chunkIndex, const red::String& jsonFilePath );

		//----

		ChunkStreamData GetChunkStreamDataForWriting(ChunkIndex chunkIndex, EMeshStreamType meshStream) const;

		/// Add new chunk stream, if it's not already created. Returns true when the creation succeeded.
		Bool AddChunkStream( ChunkIndex chunkIndex, EMeshStreamType meshStream );

		//----

		/// Call this to force recreation of material setups.
		/// You need to call Apply after this in order for the changes to actually take effect.
		void ForceRecreateMaterialSetup();
		void ForceRecreateRenderResource();
		void ForceRecreateBlob();

		/// Call this to force recalculation of data hash calculated out of vertices and triangles.
		void ForceRecalculateGeometryHash();


		/// Override save date for mesh files.
		void ForceSetSaveDate( const CDateTime& newDateTime );								/// @todo DP: this is E3 HACK, remove this!

		void MarkLODAsGenerated();

		void RemoveParameter( const rtti::ClassType* type );

		template< typename T >
		void RemoveParameter()
		{
			RemoveParameter(ClassID<T>());
		}

		// apply changed to the mesh
		// NOTE: the mesh rendering resource may be recreated and uploaded again to the GPU, this is a slow function and should not be called on job
		virtual void Apply( bool skipResourceCreation = false );

	private:
		CMesh* m_mesh;

		Bool m_recreateBlob;
		Bool m_recreateRenderResource;
		Bool m_recreateMaterialSetup;
		Bool m_recalculateGeometryHash;
	};

	/// helper class to iterate over the stream data
	template< typename T >
	class StreamIterator
	{
	public:
		RED_INLINE StreamIterator(const ChunkStreamData& data)
			: m_data(data)
			, m_pos((Uint8*) data.m_data)
			, m_end((Uint8*) data.m_data + (data.m_stride * data.m_count))
		{
			RED_FATAL_ASSERT(!data.m_data || (sizeof(T) <= data.m_stride), "Stream stride is to small to iterator type");
		}

		RED_INLINE const Bool IsValid() const
		{
			return (m_data.m_data != nullptr);
		}

		RED_INLINE const Uint32 Size() const
		{
			return m_data.m_count;
		}

		RED_INLINE operator Bool() const
		{
			return m_pos < m_end;
		}

		RED_INLINE T& Get() const
		{
			RED_FATAL_ASSERT( m_pos < m_end, "Stream iterator is outside stream range" );
			return *(const T*)m_pos;
		}

		RED_INLINE T& operator*() const
		{
			RED_FATAL_ASSERT( m_pos < m_end, "Stream iterator is outside stream range" );
			return *(T*)m_pos;
		}

		RED_INLINE T* operator->() const
		{
			RED_FATAL_ASSERT( m_pos < m_end, "Stream iterator is outside stream range" );
			return (T*)m_pos;
		}

		RED_INLINE StreamIterator<T>& operator++()
		{
			m_pos += m_data.m_stride;
			RED_FATAL_ASSERT( m_pos <= m_end, "Stream iterator moved outside stream range" );
			return *this;
		}

		RED_INLINE StreamIterator<T> operator++(int)
		{
			StreamIterator<T> cur(*this);
			m_pos += m_data.m_stride;
			RED_FATAL_ASSERT( m_pos <= m_end, "Stream iterator moved outside stream range" );
			return cur;
		}

	private:
		ChunkStreamData	m_data;
		Uint8*		m_pos;
		Uint8*		m_end;
	};

} // mesh