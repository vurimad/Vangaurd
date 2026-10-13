/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "meshParameter.h"
#include "../../redReflection/include/dataBuffer.h"

namespace physics
{
	typedef Uint8 BondHealth;
	typedef Uint16 BondIndex;

	struct DestructionHierarchyOffset;
	using DestructionHierarchyOffsets = red::DynArray< DestructionHierarchyOffset >;
}

namespace mesh
{
	struct DestructionBond
	{
		RTTI_DECLARE_TYPE( DestructionBond );

		physics::BondIndex m_bondIndex;
		physics::BondHealth m_bondHealth;
	};

	struct ChunkIndicesOffset
	{
		RTTI_DECLARE_TYPE( ChunkIndicesOffset );
		RED_USE_MEMORY_POOL( rend::PoolMesh );
		Uint32 m_start;
		Uint32 m_count;
		Uint8 m_boneIndex;
	};

	using DestructionBonds = red::DynArray< DestructionBond >;
	using DestructionBoneChunkMapping = red::DynArray<Uint64>;
	using DestructionChunkIndicesOffsets = red::DynArray< ChunkIndicesOffset >;

	class RESOURCEMESH_API MeshParamDestructionStepData : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamDestructionStepData );

	public:
		MeshParamDestructionStepData(  );

		virtual void OnMeshChanged( const CMesh& mesh ) override final;

		virtual Bool OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue ) override final;

		physics::DestructionHierarchyOffsets m_offsets = { rend::PoolMesh() };
		String m_isInstantRemovable;
	};

	class RESOURCEMESH_API MeshParamDestructionBonds : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamDestructionBonds );

	public:
		MeshParamDestructionBonds();

		void SetDestructionBonds( const DestructionBonds& bonds );
		const DestructionBonds& GetDestructionBonds();

		virtual void OnMeshChanged( const CMesh& mesh ) override final;
	private:
		DestructionBonds m_bonds = { rend::PoolMesh() };
	};

	class RESOURCEMESH_API MeshParamDestructionBoneChunkMapping : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamDestructionBoneChunkMapping );

	public:
		MeshParamDestructionBoneChunkMapping();

		const DestructionBoneChunkMapping& GetDestructionBoneChunkMapping();

		virtual void OnMeshChanged( const CMesh& mesh ) override final;
		virtual Bool OnPropertyMissing( CName propertyName, const rtti::Variant& readValue ) override final;

	private:
		DestructionBoneChunkMapping m_boneChunkMasks = { rend::PoolMesh() };
	};

	class RESOURCEMESH_API MeshParamDestructionChunkIndicesOffsets : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamDestructionChunkIndicesOffsets );

	public:
		MeshParamDestructionChunkIndicesOffsets();

		MeshParamDestructionChunkIndicesOffsets( MeshParamDestructionChunkIndicesOffsets& ) = delete;
		MeshParamDestructionChunkIndicesOffsets& operator=( MeshParamDestructionChunkIndicesOffsets& ) = delete;

		const red::ArraySpan<ChunkIndicesOffset> GetChunkIndicesOffset( Uint32 chunkIndex );

		red::ArraySpan< Uint16>  GetIndices( Uint16 chunk );
		virtual void OnMeshChanged( const CMesh& mesh ) override final;
		void Clear();

		DestructionChunkIndicesOffsets m_offsets = { rend::PoolMesh() };
		red::DynArray<Uint32> m_chunkOffsets = { rend::PoolMesh() };
		red::DynArray<DataBuffer> m_indices{ rend::PoolMesh() };
	};
	//baked destruction

	struct ChunkOffset
	{
		RTTI_DECLARE_TYPE( ChunkOffset );

		Uint32 m_chunkIndex;
		Uint16 m_start;
		Uint16 m_count;
	};

	struct RegionData
	{
		RTTI_DECLARE_TYPE( RegionData );

		red::DynArray<ChunkOffset > m_chunkDataIntact = { rend::PoolMesh() };
		red::DynArray<ChunkOffset > m_chunkDataFractured = { rend::PoolMesh() };
		Uint64 m_chunkMaskIntact = 0;
		Uint64 m_chunkMaskFractured = 0;
		Bool m_isStaticRemains = false;
	};

	enum RegionDataType : Uint16
	{
		Intact = 0,
		Fractured = 1,
		Remains = 2
	};

	using DestructionRegionData = red::DynArray< RegionData >;

	class RESOURCEMESH_API MeshParamBakedDestructionData : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamBakedDestructionData );
	public:
		MeshParamBakedDestructionData();
		
		MeshParamBakedDestructionData( MeshParamBakedDestructionData& ) = delete;
		MeshParamBakedDestructionData& operator=( MeshParamBakedDestructionData& ) = delete;

		void AppendRegionData( Uint32 regionIndeks, Uint32 chunkIndex, Uint16 start, Uint16 count, Uint16 dataType );
		virtual void OnMeshChanged( const CMesh& mesh ) override final;
		red::ArraySpan< Uint16>  GetIndices( Uint16 chunk );
		void Clear();

		DestructionRegionData m_regionData{ rend::PoolMesh() };
		red::DynArray<DataBuffer> m_indices{ rend::PoolMesh() };
	};

	class RESOURCEMESH_API MeshParamDeformableShapesData : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamDeformableShapesData );
	public:
		MeshParamDeformableShapesData();

		MeshParamDeformableShapesData( MeshParamDeformableShapesData& ) = delete;
		MeshParamDeformableShapesData& operator=( MeshParamDeformableShapesData& ) = delete;

		virtual void OnMeshChanged( const CMesh& mesh ) override;

		red::DynArray< Uint8 > m_ownerIndex{ rend::PoolMesh() };
		red::DynArray< Transform > m_startingPose{ rend::PoolMesh() };
		red::DynArray< Transform > m_finalPose{ rend::PoolMesh() };
	};

} // mesh
