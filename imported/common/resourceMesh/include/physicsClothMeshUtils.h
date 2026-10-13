/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class IRenderResource;

namespace mesh
{
	using LODIndex = Uint8;

	class DataViewRO;

	struct Edge
	{
		Uint32 i, j;
	};

	struct AdjacentTriangles
	{
		red::DynArray<Uint32> triangles{ red::PoolResource() };
	};

	struct EdgeList
	{
		red::DynArray<Edge> edges{ red::PoolResource() };
	};

	struct TriangleParticles
	{
		Vector3 p0;
		Vector3 p1;
		Vector3 p2;
	};

	struct FaceNormal
	{
		Vector3 start;
		Vector3 end;
	};

	using AdjacentTrianglesMap = red::Map<Uint32, AdjacentTriangles>;
	using EdgesMap = red::Map<Uint32, EdgeList>;
	using NormalsArray = red::DynArray<Vector3>;
	using TriangleIdx = Uint32;
	using TriangleList = red::DynArray<TriangleIdx>;
	using TriangleSpan = red::ArraySpan<const TriangleIdx>;

	struct MeshTopologyInfo
	{
		AdjacentTrianglesMap indexToTriangleMap{ red::PoolResource() };
		EdgesMap indexToEdgeMap{ red::PoolResource() };
		NormalsArray faceNormals{ red::PoolResource() };
		red::DynArray<Vector3> faceCentroids{ red::PoolResource() };
		Uint32 numTriangles = 0;
	};

	struct ClothMeshChunkSkinningInfo
	{
		RED_USE_MEMORY_POOL(red::PoolResource); 

		red::DynArray<Uint32> indexToTriangle{ red::PoolResource() };
		red::DynArray<Vector4> barycentrics{ red::PoolResource() };
	};

	using ClothLodSwitchData = red::StaticArray<Int32,2>;
	struct ClothMeshTopologyInfo
	{
		red::DynArray<ClothMeshChunkSkinningInfo> gfxChunkSkinningInfo{ red::PoolResource() };
		red::DynArray<ClothMeshChunkSkinningInfo> phxLodSkinningInfo{ red::PoolResource() };
		red::DynArray<ClothLodSwitchData> phxLodTransitionData{ red::PoolResource() };
		red::DynArray<red::DynArray<Bool>> simulated{ red::PoolResource() };

		static constexpr Int32 up = 0;
		static constexpr Int32 down = 1;
	};

	struct RESOURCEMESH_API CookedClothMeshTopologyData
	{
		RTTI_DECLARE_TYPE(CookedClothMeshTopologyData);

		DataBuffer m_cookedData;

		red::DynArray<Uint32> m_gfxIndexToTriangles{ red::PoolResource() };
		red::DynArray<Uint32> m_phxIndexToTriangles{ red::PoolResource() };
		red::DynArray<Uint32> m_gfxBarycentrics{ red::PoolResource() };
		red::DynArray<Uint32> m_phxBarycentrics{ red::PoolResource() };
		red::DynArray<Uint32> m_phxLodSwitchData{ red::PoolResource() };
		red::DynArray<Uint32> m_phxSimulated{ red::PoolResource() };

		Uint32 m_gfxNumIndicesToTriangles;
		Uint32 m_phxNumIndicesToTriangles;
		Uint32 m_gfxNumBarycentrics;
		Uint32 m_phxNumBarycentrics;
		Uint32 m_phxNumLodSwitchData;
		Uint32 m_phxNumSimulated;
	};

	RESOURCEMESH_API MeshTopologyInfo CreateClothMeshTopologyInfo(red::BlobView view);
	RESOURCEMESH_API red::ArraySpan<const Uint32> GetAdjacentTriangles(Uint32 index, const MeshTopologyInfo& info);
	RESOURCEMESH_API red::ArraySpan<const Vector3> GetFaceNormals(const MeshTopologyInfo& info);
	RESOURCEMESH_API red::ArraySpan<const Vector3> GetFaceCentroids(const MeshTopologyInfo& info);
	RESOURCEMESH_API red::ArraySpan<const Edge> GetEdges(const MeshTopologyInfo& info);

	RESOURCEMESH_API red::ArraySpan<const Vector4> ExtractBarycentrics(const ClothMeshTopologyInfo& info, Uint32 chunk);
	RESOURCEMESH_API red::ArraySpan<const Bool> ExtractIsSkinned(const ClothMeshTopologyInfo& info, Uint32 chunk);
	RESOURCEMESH_API red::ArraySpan<const Uint32> ExtractVertexToTriangleMapping(const ClothMeshTopologyInfo& info, Uint32 chunk);

	RESOURCEMESH_API ClothMeshTopologyInfo CreateClothMeshTopologyInfo(const red::memory::Pool& pool, const DataViewRO& phx, const DataViewRO& gfx);
	RESOURCEMESH_API ClothMeshTopologyInfo CreateClothMeshTopologyInfo(const red::memory::Pool& pool, const CookedClothMeshTopologyData& data);
	RESOURCEMESH_API void CookClothMeshTopologyInfo(const red::memory::Pool& pool, const DataViewRO& phx, const DataViewRO& gfx, CookedClothMeshTopologyData& out);

	template< typename Pool >
	ClothMeshTopologyInfo CreateClothMeshTopologyInfo(const DataViewRO& phx, const DataViewRO& gfx)
	{
		return CreateClothMeshTopologyInfo(Pool::GetInstance(), phx, gfx);
	}

	template< typename Pool >
	ClothMeshTopologyInfo CreateClothMeshTopologyInfo(const CookedClothMeshTopologyData& data)
	{
		return CreateClothMeshTopologyInfo(Pool::GetInstance(), data);
	}

	template< typename Pool >
	void CookClothMeshTopologyInfo(const DataViewRO& phx, const DataViewRO& gfx, CookedClothMeshTopologyData& out)
	{
		CookClothMeshTopologyInfo(Pool::GetInstance(), phx, gfx, out);
	}
}