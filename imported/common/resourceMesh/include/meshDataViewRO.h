#pragma once

#include "meshChunk.h"

class IMaterial;
class CMesh;
class CMaterialInstance;

namespace mesh
{
	class MeshAppearance;
    class MeshParameter;

	typedef Uint8 LODIndex;
	typedef Uint16 ChunkIndex;
	typedef Uint16 MaterialIndex;
	typedef Uint8 ChunkMaterialIndex;
	typedef Uint16 BoneIndex;
	typedef Uint16 FloatTrackIndex;

	struct RESOURCEMESH_API ChunkStreamDataRO
	{
		EMeshStreamType m_stream;
		const void* m_data;
		Uint32 m_stride;
		Uint32 m_count;

		serialization::BufferHandleRO m_buffer;

		RED_FORCE_INLINE ChunkStreamDataRO()
			: m_stream(MST_None)
			, m_data(nullptr)
			, m_stride(0)
			, m_count(0)
		{}

		RED_INLINE Bool Empty() const { return m_data == nullptr; }
	};

	struct RESOURCEMESH_API ChunkFlags
	{
		RTTI_DECLARE_TYPE(ChunkFlags);

		Bool m_renderInScene;
		Bool m_renderInShadows;	// If true, then size- and autohide- based heuristics will decide to what specific global shadowmap it gets rendered (eg. cascade2, distant shadows). Mesh node flag on the other hand will decide whether it goes to local shadowmap.
		Bool m_isTwoSided;
		Bool m_isRayTracedEmissive;

		RED_FORCE_INLINE ChunkFlags()
			: m_renderInScene(true)
			, m_renderInShadows(true)
			, m_isTwoSided(false)
			, m_isRayTracedEmissive(false)
		{}
	};

	/// read only data view of the mesh
	/// always safe to instantiate
	class RESOURCEMESH_API DataViewRO
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		DataViewRO(const CMesh& mesh);
		virtual ~DataViewRO();

		const CMesh* GetMesh() const { return m_meshRO; }

		LODIndex GetNumLODs() const;
		ChunkIndex GetNumChunks() const;
		MaterialIndex GetNumMaterials() const;
		BoneIndex GetNumBones() const;
		BoneIndex GetNumBoneRigMatrices() const;
		Uint32 GetNumAppearances() const;
		Uint32 GetNumFloatTracks() const;

		//---

		Float GetAutoHideDistance( const Vector3& scale = Vector3::ONES() ) const;

		//---

		Float GetLODDistance(LODIndex lodIndex) const;
		Uint32 GetLODChunkCount( LODIndex lodIndex ) const;

		const red::DynArray<Uint16> GetLODChunkIndices( LODIndex lodIndex ) const;

		//---

		Uint8 GetChunkLodMask(ChunkIndex chunkIndex) const;
		Uint64 GetChunkStreamMask(ChunkIndex chunkIndex) const;
		EMeshVertexType GetChunkVertexType(ChunkIndex chunkIndex) const;
		Uint32 GetChunkVertexCount(ChunkIndex chunkIndex) const;
		Uint32 GetChunkIndexCount(ChunkIndex chunkIndex) const;
		Uint32 GetChunkTriangleCount( ChunkIndex chunkIndex ) const;
		ChunkStreamDataRO GetChunkStreamData( ChunkIndex chunkIndex, EMeshStreamType meshStream ) const;
		Uint8 GetChunkNumBonesPerVertex( ChunkIndex chunkIndex ) const;
		Uint8 GetChunkMaterialCount(ChunkIndex chunkIndex) const;
		CName GetChunkMaterialName(ChunkIndex chunkIndex, ChunkMaterialIndex index) const;
		ChunkFlags GetChunkFlags( ChunkIndex chunkIndex ) const;
		const red::DynArray< CName >& GetChunkMaterialNames( ChunkIndex chunkIndex ) const;
		const String& GetChunkSourceMeshUUID( ChunkIndex chunkIndex ) const;
		const String& GetChunkSourceMaterialUUID( ChunkIndex chunkIndex ) const;
		Double GetChunkSurfaceArea( ChunkIndex chunkIndex ) const;

		Uint64 CalculateGeometryHash() const;

		//---

		CName GetBoneName(BoneIndex boneIndex) const;
		Matrix GetBoneMatrix(BoneIndex boneIndex) const;
		Bool FindBoneIndex( CName boneName, BoneIndex& index ) const;
		Float GetBoneVertexEpsilon(BoneIndex boneIndex) const;

		const red::DynArray< CName >& GetBoneNames() const;
		const red::DynArray< Matrix >& GetBoneRigMatrices() const;
		const red::DynArray< Float >& GetBoneVertexEpsilons() const;

		CName GetFloatTrackName(FloatTrackIndex trackIndex) const;
		const red::DynArray< CName >& GetFloatTrackNames() const;

		void CalcChunkSkinningHistogram( ChunkIndex chunkIndex, red::StaticArray<Uint32, 8+1>& skinningHistogram,
		                                 red::DynArray<Uint32>* boneUseHistogram = nullptr ) const;

		//---

		CName GetMaterialName(MaterialIndex index) const;
		THandle< IMaterial > GetMaterialData( MaterialIndex index ) const;
		THandle< IMaterial > GetMaterialDataByName( CName materialName ) const;
		Int32 GetMaterialIndex( CName materialName ) const;
		Int32 GetMaterialIndex( const IMaterial* material ) const;
		Bool IsMaterialLocalInstance( MaterialIndex index ) const;
		Bool IsMaterialLocalInstance( const IMaterial* material ) const;
		Bool IsMaterialTemplate( MaterialIndex index ) const;
		red::DynArray< THandle< IMaterial > > GetMaterials() const;
		red::DynArray< CName > GetMaterialNames() const;
		const red::DynArray< TResAsyncRef<IMaterial> >& GetExternalMaterials() const;
		red::DynArray< THandle<CMaterialInstance> > GetLoadedLocalMaterialInstances() const;

		//---

		CName GetAppearanceName( Uint32 appearanceIndex ) const;
		Int32 GetAppearanceIndex( CName appearanceName ) const;
		Int32 GetAppearanceIndex( const String& appearanceName ) const;
		ChunkIndex GetAppearanceChunkCount( Uint32 appearanceIndex, Int32 lodIndex = -1 ) const;          // Get appearance chunk count for specific LOD or for all LODs (-1)
		Uint8 GetAppearanceChunkMaterialCount( Uint32 appearanceIndex, ChunkIndex chunkIndex ) const;
		CName GetAppearanceMaterialName( Uint32 appearanceIndex, ChunkIndex chunkIndex, ChunkMaterialIndex materialIndex ) const;
		THandle< IMaterial > GetAppearanceMaterialData( Uint32 appearanceIndex, ChunkIndex chunkIndex, ChunkMaterialIndex materialIndex ) const;
		const THandle< MeshAppearance >& GetAppearanceData( Uint32 appearanceIndex ) const;
		const THandle< MeshAppearance >& GetDefaultAppearanceData() const;

		Bool HasParameter(const rtti::ClassType* type) const;
        THandle<MeshParameter> GetParameter(const rtti::ClassType* type) const;

		const Uint32 PackFlags(const mesh::ChunkFlags& chunkFlags);
		const mesh::ChunkFlags UnpackFlags(const Int32& packedFlags);

		template<typename T> 
        Bool HasParameter() const
		{
            return HasParameter(ClassID<T>());
        }

        template<typename T> 
        THandle<T> GetParameter() const
		{
            return Cast<T>(GetParameter(ClassID<T>()));
        }

        red::ArraySpan<const Uint8> GetBonesLODMask() const;

	protected:
		CName GetMaterialName_NoLock(MaterialIndex index) const;

		const CMesh* m_meshRO;
	};

	RESOURCEMESH_API DataViewRO MakeView(CMesh* mesh);
	RESOURCEMESH_API DataViewRO MakeView(const CMesh& mesh);
	RESOURCEMESH_API DataViewRO MakeView(CMesh& mesh);
	RESOURCEMESH_API DataViewRO MakeView(const THandle<CMesh>& mesh);
	RESOURCEMESH_API DataViewRO MakeView(const TResRef<CMesh>& mesh);

	/// helper class to iterate over the stream data
	template< typename T >
	class StreamIteratorRO
	{
	public:
		RED_INLINE StreamIteratorRO() : m_pos( nullptr ), m_end( nullptr ) {}

		RED_INLINE StreamIteratorRO(const ChunkStreamDataRO& data)
			: m_data(data)
			, m_pos((const Uint8*) data.m_data)
			, m_end((const Uint8*) data.m_data + (data.m_stride * data.m_count))
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

		RED_INLINE const T& Get() const
		{
			RED_FATAL_ASSERT( m_pos < m_end, "Stream iterator is outside stream range" );
			return *(const T*)m_pos;
		}

		RED_INLINE const T& operator*() const
		{
			RED_FATAL_ASSERT( m_pos < m_end, "Stream iterator is outside stream range" );
			return *(const T*)m_pos;
		}

		RED_INLINE const T* operator->() const
		{
			RED_FATAL_ASSERT( m_pos < m_end, "Stream iterator is outside stream range" );
			return (const T*)m_pos;
		}

		RED_INLINE const T& operator[]( Uint32 idx ) const
		{
			RED_FATAL_ASSERT( m_pos + idx * m_data.m_stride >= (const Uint8*)m_data.m_data && m_pos + idx * m_data.m_stride < m_end, "Stream iterator [ %u ] is outside stream range", idx );
			return *(const T*)( m_pos + idx * m_data.m_stride );
		}

		RED_INLINE StreamIteratorRO<T>& operator++()
		{
			m_pos += m_data.m_stride;
			RED_FATAL_ASSERT( m_pos <= m_end, "Stream iterator moved outside stream range" );
			return *this;
		}

		RED_INLINE StreamIteratorRO<T> operator+=( Int32 elements )
		{
			m_pos += m_data.m_stride * elements;
			RED_FATAL_ASSERT( m_pos >= (const Uint8*)m_data.m_data && m_pos <= m_end, "Stream iterator moved outside stream range" );
			return *this;
		}

		RED_INLINE StreamIteratorRO<T> operator-=( Int32 elements )
		{
			m_pos -= m_data.m_stride * elements;
			RED_FATAL_ASSERT( m_pos >= (const Uint8*)m_data.m_data && m_pos <= m_end, "Stream iterator moved outside stream range" );
			return *this;
		}

		RED_INLINE StreamIteratorRO<T> operator++(int)
		{
			StreamIteratorRO<T> cur(*this);
			m_pos += m_data.m_stride;
			RED_FATAL_ASSERT( m_pos <= m_end, "Stream iterator moved outside stream range" );
			return cur;
		}

	private:
		ChunkStreamDataRO	m_data;
		const Uint8*		m_pos;
		const Uint8*		m_end;
	};

	/// helper class for array like view of the stream
	template< typename T >
	class StreamViewRO
	{
	public:
		RED_INLINE StreamViewRO(const ChunkStreamDataRO& data)
			: m_data(data)
		{
			RED_FATAL_ASSERT(!data.m_data || (sizeof(T) <= data.m_stride), "Stream stride is to small to iterator type");
		}

		RED_INLINE const Uint32 Size() const
		{
			return m_data.m_count;
		}

		RED_INLINE const Bool IsValid() const
		{
			return (m_data.m_data != nullptr);
		}

		RED_INLINE const T& operator[](const Uint32 index) const
		{
			RED_FATAL_ASSERT( IsValid() && index < Size(), "Stream view [ %u ] is outside stream range ( %u )", index, Size() );
			return *(const T*)( ((const Uint8*)m_data.m_data) + (m_data.m_stride * index));
		}

	private:
		ChunkStreamDataRO	m_data;
	};

    template< typename T >
    red::ArraySpan<T> MakeSpan(const mesh::ChunkStreamDataRO& stream)
    {
        return red::MakeArraySpan((T*)stream.m_data, stream.m_count);
    }

    template< typename T >
    red::ArraySpan<T> MakeSpan(const mesh::DataViewRO& view, ChunkIndex chunk, EMeshStreamType type)
    {
        auto stream = view.GetChunkStreamData(chunk, type);
        return MakeSpan<T>(stream);
    }

} // mesh
