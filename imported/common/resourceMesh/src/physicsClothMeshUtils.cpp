#include "build.h"
#include "physicsClothMeshUtils.h"
#include "meshDataViewRO.h"
#include "meshParamCloth.h"

#include "../../../common/core/include/dataBufferUtils.h"
#include "../../../common/physics/include/physXNatives.h"

#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/renderData/include/renderResource.h"
#include "../../../common/renderData/include/renderer.h"

#include "../../../common/redMath/include/mathUtils.h"
#include "../../../common/redReflection/include/mathUtils.h"

#ifdef PHYSICS_DEBUG
#pragma optimize("", off)
#endif

RTTI_BEGIN_TYPE_IN_NAMESPACE(CookedClothMeshTopologyData, mesh)
	RTTI_PROPERTY(m_cookedData);
	RTTI_PROPERTY(m_gfxIndexToTriangles);
	RTTI_PROPERTY(m_phxIndexToTriangles);
	RTTI_PROPERTY(m_gfxBarycentrics);
	RTTI_PROPERTY(m_phxBarycentrics);
	RTTI_PROPERTY(m_phxLodSwitchData);
	RTTI_PROPERTY(m_phxSimulated);
	RTTI_PROPERTY(m_gfxNumIndicesToTriangles);
	RTTI_PROPERTY(m_phxNumIndicesToTriangles);
	RTTI_PROPERTY(m_gfxNumBarycentrics);
	RTTI_PROPERTY(m_phxNumBarycentrics);
	RTTI_PROPERTY(m_phxNumLodSwitchData);
	RTTI_PROPERTY(m_phxNumSimulated);
RTTI_END_TYPE();

namespace mesh
{
	namespace internal
	{
		struct Triangle
		{
			union
			{
				Uint32 i[3];
				struct { Uint32 i0, i1, i2; };
			};
			
		};

		Triangle ConvertTriangleIndex(red::ArraySpan<const Uint32> indices, Uint32 tri)
		{
			Triangle result;

			result.i0 = indices[3*tri+0];
			result.i1 = indices[3*tri+1];
			result.i2 = indices[3*tri+2];

			return result;
		}
	}

	MeshTopologyInfo CreateClothMeshTopologyInfo(red::BlobView view)
	{
		MeshTopologyInfo result;

		auto indices = physics::cloth::ExtractTriangles(view);
		auto particles = physics::cloth::ExtractRestPositions(view);

		for(Uint32 i = 0; i < indices.Size(); i+=3)
		{
			Uint32 i0 = indices[i+0];
			Uint32 i1 = indices[i+1];
			Uint32 i2 = indices[i+2];

			result.indexToTriangleMap[i0].triangles.PushBack(i/3);
			result.indexToTriangleMap[i1].triangles.PushBack(i/3);
			result.indexToTriangleMap[i2].triangles.PushBack(i/3);

			auto p0 = particles[i0].AsVector3();
			auto p1 = particles[i1].AsVector3();
			auto p2 = particles[i2].AsVector3();

			auto p = p1 - p0;
			auto q = p2 - p0;
			result.faceNormals.PushBack(q.Cross(p).Normalized());
			result.faceCentroids.PushBack((p0 + p1 + p2) / 3.f);
		}

		return result;
	}

	Uint32 FindNearestTriangle(const Vector4& hires, red::ArraySpan<const Vector4> particles, red::ArraySpan<const Uint32> indices)
	{
		Uint32 result = -1;
		Float distanceSq = RED_FLT_MAX;
		const Uint32 num_triangles = indices.Size() / 3;
		for( Uint32 tri = 0; tri < num_triangles; tri++ )
		{
			auto triangle = internal::ConvertTriangleIndex(indices, tri);

			const auto p0 = particles[triangle.i0];
			const auto p1 = particles[triangle.i1];
			const auto p2 = particles[triangle.i2];
			const auto plane = math::Plane(p0, p1, p2);
			const auto projected_p = plane.Project(hires);

			auto closest_point = math::utils::ClosestPointOnTriangle(p0, p1, p2, projected_p);
			const Float currDistSq = closest_point.DistanceSquaredTo(hires);
			if( currDistSq < distanceSq )
			{
				distanceSq = currDistSq;
				result = tri;
			}
		}

		return result;
	}

	Int32 FindNearestParticle(const Vector4& hires, Uint32 triIndex, red::ArraySpan<const Vector4> particles, red::ArraySpan<const Uint32> indices)
	{
		auto triangle = internal::ConvertTriangleIndex(indices, triIndex);
		const auto p0 = particles[triangle.i0];
		const auto p1 = particles[triangle.i1];
		const auto p2 = particles[triangle.i2];

		const Float d0 = p0.DistanceSquaredTo(hires);
		const Float d1 = p1.DistanceSquaredTo(hires);
		const Float d2 = p2.DistanceSquaredTo(hires);

		const auto min = Min(d0, d1, d2);
		if( min == d0 )
		{
			return triangle.i0;
		}
		else if( min == d1 )
		{
			return triangle.i1;
		}

		return triangle.i2;
	}
	
	Int32 FindNearestParticle(const Vector4& p, red::ArraySpan<const Vector4> particles)
	{
		Int32 nearest = -1;
		Float distanceSq = RED_FLT_MAX;

		for( auto idx : particles.Indices() )
		{
			const Float currDistSq = p.DistanceSquaredTo( particles[idx] );
			if( currDistSq < distanceSq )
			{
				distanceSq = currDistSq;
				nearest = static_cast<Int32>(idx);
			}
		}

		return nearest;
	}

	Vector4 FillBarycentricInfo( const Vector3& h, Uint32 nearestTriangle, const red::ArraySpan<const Vector4> lowres_particles, const mesh::TriangleSpan lowres_triangles )
	{
		const auto triangle = internal::ConvertTriangleIndex(lowres_triangles, nearestTriangle);
		const auto p0 = lowres_particles[triangle.i0];
		const auto p1 = lowres_particles[triangle.i1];
		const auto p2 = lowres_particles[triangle.i2];

		math::Plane plane(p0,p1,p2);
		Vector4 result;

		auto projected_p = plane.Project(h).AsVector3();
		if( !MathUtils::GeometryUtils::IsPointInsideTriangle(p0, p1, p2, projected_p))
		{
			projected_p = math::utils::ClosestPointOnTriangle(p0, p1, p2, projected_p);
		}

		result = math::utils::ComputeBaryCentric(projected_p, p0, p1, p2);
		if(projected_p.DistanceTo(p0) < 1.0e-6f)
		{
			result = Vector3::EX();
		}
		else if(projected_p.DistanceTo(p1) < 1.0e-6f)
		{
			result = Vector3::EY();
		}
		else if(projected_p.DistanceTo(p2) < 1.0e-6f)
		{
			result = Vector3::EZ();
		}
		else
		{
			const Float e0 = 1.f - (result.X + result.Y);
			const Float e1 = 1.f - (result.X + result.Z);
			const Float e2 = 1.f - (result.Y + result.Z);
			if(e0 < 1.0e-6f)
			{
				result.Z = 0.f;
			}
			else if(e1 < 1.0e-6f)
			{
				result.Y = 0.f;
			}
			else if( e2 < 1.0e-6f)
			{
				result.X = 0.f;
			}
		}

		result.W = plane.DistanceTo(h);
		return result;
	}

	void CreateLODMapping(ClothMeshChunkSkinningInfo& info, red::BlobView fromView, red::BlobView toView)
	{
		const auto to_particles = physics::cloth::ExtractRestPositions(toView);
		const auto from_particles = physics::cloth::ExtractRestPositions(fromView);
		const auto from_triangles = physics::cloth::ExtractTriangles(fromView);

		info.indexToTriangle.Resize(to_particles.Size(), -1);
		info.barycentrics.Resize(to_particles.Size());
		for( Uint32 i : to_particles.Indices() )
		{
			const auto& p = to_particles[i];

			info.indexToTriangle[i] = FindNearestTriangle(p, from_particles, from_triangles);
			info.barycentrics[i] = FillBarycentricInfo(p, info.indexToTriangle[i], from_particles, from_triangles);
		}
	}

	ClothMeshTopologyInfo CreateClothMeshTopologyInfo(const red::memory::Pool& pool, const DataViewRO& phx, const DataViewRO& gfx)
	{
		ClothMeshTopologyInfo result;
		auto param = phx.GetParameter<MeshParamCloth>();
		RED_FATAL_ASSERT(IsValid(param), "Error: Cloth param not present in physical mesh");

		//Do graphical mesh to physical proxy mapping info
		for( Uint32 lod = 0; lod < phx.GetNumLODs(); ++lod )
		{
			const auto view = param->m_chunks[lod].m_cookedData.GetView();
			const auto lowres_particles = physics::cloth::ExtractRestPositions(view);
			const auto lowres_triangles = physics::cloth::ExtractTriangles(view);

			const auto lodChunkIndices = mesh::ExtractLodChunkIndices(gfx, lod);
			result.gfxChunkSkinningInfo.Grow( lodChunkIndices.Size() );
			result.simulated.Resize(result.simulated.Size() + lodChunkIndices.Size(), {pool});

			for( auto& chunk : lodChunkIndices )
			{
				auto& info = result.gfxChunkSkinningInfo[chunk];
				auto hires = mesh::ExtractPositions(gfx, chunk);
				const auto simulationIndices = mesh::ExtractSimulationIndices(gfx, chunk);

				result.simulated[chunk].Resize(hires.Size(), false);
				info.barycentrics.Resize(hires.Size());
				info.indexToTriangle.Resize(hires.Size(), -1);

				for(Uint32 i : hires.Indices())
				{
					if( std::binary_search(begin(simulationIndices), end(simulationIndices), i) )
					{
						result.simulated[chunk][i] = true;
						const auto& h = hires[i];

						info.indexToTriangle[i] = FindNearestTriangle(h, lowres_particles, lowres_triangles);
						info.barycentrics[i] = FillBarycentricInfo(h, info.indexToTriangle[i], lowres_particles, lowres_triangles); 
					}
				}
			}
		}

		for( Int32 lod = 0; lod < phx.GetNumLODs(); ++lod )
		{
			const Int32 curr_lod = lod;
			const Int32 prev_lod = lod - 1;
			const Int32 next_lod = lod + 1;

			ClothLodSwitchData transition_data({-1, -1});
			if( next_lod < phx.GetNumLODs() )
			{
				transition_data[ClothMeshTopologyInfo::up] = result.phxLodSkinningInfo.Size();
				auto& info = result.phxLodSkinningInfo.EmplaceBack();
				
				auto currView = param->m_chunks[curr_lod].m_cookedData.GetView();
				auto nextView = param->m_chunks[next_lod].m_cookedData.GetView();
				CreateLODMapping(info, currView, nextView);
			}

			if( prev_lod >= 0 )
			{
				transition_data[ClothMeshTopologyInfo::down] = result.phxLodSkinningInfo.Size();
				auto& info = result.phxLodSkinningInfo.EmplaceBack();

				auto currView = param->m_chunks[curr_lod].m_cookedData.GetView();
				auto prevView = param->m_chunks[prev_lod].m_cookedData.GetView();
				CreateLODMapping(info, currView, prevView);
			}

			result.phxLodTransitionData.PushBack(transition_data);
		}

        return result;
	}

	ClothMeshTopologyInfo CreateClothMeshTopologyInfo(const red::memory::Pool& pool, const CookedClothMeshTopologyData& data)
	{
		ClothMeshTopologyInfo result;

		DataPartionerHelper helper(data.m_cookedData.Data(), data.m_cookedData.Size());

		red::ArraySpan<const Uint32> gfxIndexToTriangle = helper.Span<Uint32>(data.m_gfxNumIndicesToTriangles);
		red::ArraySpan<const Vector4> gfxBarycentrics = helper.Span<Vector4>(data.m_gfxNumBarycentrics);
		red::ArraySpan<const Uint32> phxIndexToTriangle = helper.Span<Uint32>(data.m_phxNumIndicesToTriangles);
		red::ArraySpan<const Vector4> phxBarycentrics = helper.Span<Vector4>(data.m_phxNumBarycentrics);
		red::ArraySpan<const Int32> phxLodSwitchData = helper.Span<Int32>(data.m_phxNumLodSwitchData);
		red::ArraySpan<const Bool> phxSimulated = helper.Span<Bool>(data.m_phxNumSimulated);

		Uint32 current_offset = 0;
		for( Uint32 size : data.m_gfxIndexToTriangles )
		{
			ClothMeshChunkSkinningInfo& chunk = result.gfxChunkSkinningInfo.EmplaceBack();
			chunk.indexToTriangle.PushBack(gfxIndexToTriangle.SubSpan(current_offset, size));
			current_offset += size;
		}
		
		current_offset = 0;
		for( Uint32 idx : data.m_gfxBarycentrics.Indices() )
		{
			const Uint32 size = data.m_gfxBarycentrics[idx];
			ClothMeshChunkSkinningInfo& chunk = result.gfxChunkSkinningInfo[idx];
			chunk.barycentrics.PushBack(gfxBarycentrics.SubSpan(current_offset, size));
			current_offset += size;
		}

		current_offset = 0;
		for( Uint32 size : data.m_phxIndexToTriangles )
		{
			ClothMeshChunkSkinningInfo& chunk = result.phxLodSkinningInfo.EmplaceBack();
			chunk.indexToTriangle.PushBack(phxIndexToTriangle.SubSpan(current_offset, size));
			current_offset += size;
		}
		
		current_offset = 0;
		for( Uint32 idx : data.m_phxBarycentrics.Indices() )
		{
			const Uint32 size = data.m_phxBarycentrics[idx];
			ClothMeshChunkSkinningInfo& chunk = result.phxLodSkinningInfo[idx];
			chunk.barycentrics.PushBack(phxBarycentrics.SubSpan(current_offset, size));
			current_offset += size;
		}
		
		current_offset = 0;
		for( Uint32 size : data.m_phxLodSwitchData )
		{
			ClothLodSwitchData& chunk = result.phxLodTransitionData.EmplaceBack();
			chunk.Resize(size);
			chunk[0] = phxLodSwitchData[current_offset + 0];
			chunk[1] = phxLodSwitchData[current_offset + 1];
			current_offset += size;
		}
		
		current_offset = 0;
		for( Uint32 size : data.m_phxSimulated )
		{
			auto& chunk = result.simulated.EmplaceBack(pool);
			chunk.PushBack( phxSimulated.SubSpan(current_offset, size) );
			current_offset += size;
		}

		return result;
	}

	void CookClothMeshTopologyInfo(const red::memory::Pool& pool, const DataViewRO& phx, const DataViewRO& gfx, CookedClothMeshTopologyData& out)
	{
		auto topology = CreateClothMeshTopologyInfo(pool, phx, gfx);

		red::DynArray<Uint32> gfx_indexToTriangle { pool };
		red::DynArray<Uint32> phx_indexToTriangle { pool };
		red::DynArray<Vector4> gfx_barycentrics { pool };
		red::DynArray<Vector4> phx_barycentrics { pool };
		red::DynArray<Int32> phx_lodSwitchData{ pool };
		red::DynArray<Bool> phx_simulatedData{ pool };

		for( const ClothMeshChunkSkinningInfo& chunk : topology.gfxChunkSkinningInfo )
		{
			gfx_indexToTriangle.PushBack(chunk.indexToTriangle);
			gfx_barycentrics.PushBack(chunk.barycentrics);

			out.m_gfxIndexToTriangles.PushBack(chunk.indexToTriangle.Size());
			out.m_gfxBarycentrics.PushBack(chunk.barycentrics.Size());
		}

		for( const ClothMeshChunkSkinningInfo& chunk : topology.phxLodSkinningInfo )
		{
			phx_indexToTriangle.PushBack(chunk.indexToTriangle);
			phx_barycentrics.PushBack(chunk.barycentrics);

			out.m_phxIndexToTriangles.PushBack(chunk.indexToTriangle.Size());
			out.m_phxBarycentrics.PushBack(chunk.barycentrics.Size());
		}

		for( const ClothLodSwitchData& data : topology.phxLodTransitionData )
		{
			phx_lodSwitchData.PushBack(data[0]);
			phx_lodSwitchData.PushBack(data[1]);

			out.m_phxLodSwitchData.PushBack(data.Size());
		}

		for( const auto& simulated : topology.simulated )
		{
			phx_simulatedData.PushBack(simulated);
			out.m_phxSimulated.PushBack(simulated.Size());
		}

		out.m_gfxNumIndicesToTriangles = gfx_indexToTriangle.Size();
		out.m_phxNumIndicesToTriangles = phx_indexToTriangle.Size();
		out.m_gfxNumBarycentrics = gfx_barycentrics.Size();
		out.m_phxNumBarycentrics = phx_barycentrics.Size();
		out.m_phxNumLodSwitchData = phx_lodSwitchData.Size();
		out.m_phxNumSimulated = phx_simulatedData.Size();

		Uint32 totalSizeInBytes = gfx_indexToTriangle.DataSize();
		totalSizeInBytes += gfx_barycentrics.DataSize();
		totalSizeInBytes += phx_indexToTriangle.DataSize();
		totalSizeInBytes += phx_barycentrics.DataSize();
		totalSizeInBytes += phx_lodSwitchData.DataSize();
		totalSizeInBytes += phx_simulatedData.DataSize();

		DataBuffer data( totalSizeInBytes, 16, pool );
		DataCopyHelper helper( data.Data(), totalSizeInBytes );
		helper.AppendArray( gfx_indexToTriangle );
		helper.AppendArray( gfx_barycentrics );
		helper.AppendArray( phx_indexToTriangle );
		helper.AppendArray( phx_barycentrics );
		helper.AppendArray( phx_lodSwitchData );
		helper.AppendArray( phx_simulatedData );

		out.m_cookedData = DataBuffer::Copy(data);
	}

	red::ArraySpan<const Uint32> GetAdjacentTriangles(Uint32 index, const MeshTopologyInfo& info)
	{
		if( info.indexToTriangleMap.KeyExist(index) )
		{
			return { info.indexToTriangleMap[index].triangles };
		}

		return {};
	}

	red::ArraySpan<const Vector3> GetFaceNormals(const MeshTopologyInfo& info)
	{
		return { info.faceNormals };
	}

	red::ArraySpan<const Vector3> GetFaceCentroids(const MeshTopologyInfo& info)
	{
		return { info.faceCentroids };
	}

	red::ArraySpan<const Edge> GetEdges(Uint32 index, const MeshTopologyInfo& info)
	{
		if( info.indexToTriangleMap.KeyExist(index) )
		{
			return { info.indexToEdgeMap[index].edges };
		}

		return {};
	}

	red::ArraySpan<const Vector4> ExtractBarycentrics(const ClothMeshTopologyInfo& info, Uint32 chunk)
	{
		return { info.gfxChunkSkinningInfo[chunk].barycentrics };
	}

	red::ArraySpan<const Bool> ExtractIsSkinned(const ClothMeshTopologyInfo& info, Uint32 chunk)
	{
		return { info.simulated[chunk] };
	}

	red::ArraySpan<const Uint32> ExtractVertexToTriangleMapping(const ClothMeshTopologyInfo& info, Uint32 chunk)
	{
		return { info.gfxChunkSkinningInfo[chunk].indexToTriangle };
	}
}

#ifdef PHYSICS_DEBUG
#pragma optimize("", on)
#endif