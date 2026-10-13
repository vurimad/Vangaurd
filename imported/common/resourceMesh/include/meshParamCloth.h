/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "meshParameter.h"
#include "../../../common/redReflection/include/dataBuffer.h"
#include "../../../common/physics/include/physicsClothStructs.h"

namespace mesh
{
    using IndexList = red::DynArray<Uint16>;
    using ChannelList = red::DynArray<IndexList>;
	using LODIndex = Uint8;

    using ExportedCapsules = physics::cloth::ClothCapsuleExportData;

    class DataViewRO;

    struct RESOURCEMESH_API RawClothData
    {
        RTTI_DECLARE_TYPE(RawClothData);

        RawClothData();

		RawClothData(const RawClothData&) = delete;
        RawClothData& operator=(RawClothData&) = delete;

        physics::cloth::State m_state;

		red::DynArray<DataBuffer> m_maxDistanceChannel{red::PoolResource()};
		red::DynArray<DataBuffer> m_maxDistanceExtChannel{red::PoolResource()};
        red::DynArray<DataBuffer> m_backstopDistanceChannel{red::PoolResource()};
		red::DynArray<DataBuffer> m_backstopRadiusChannel{red::PoolResource()};
        red::DynArray<DataBuffer> m_selfCollisionChannel{red::PoolResource()};
    };

	struct RESOURCEMESH_API PhxClothChunkData
	{
		RTTI_DECLARE_TYPE(PhxClothChunkData);

		DataBuffer m_positions;
        DataBuffer m_indices;
        DataBuffer m_skinWeights;
        DataBuffer m_skinIndices;
        DataBuffer m_skinWeightsExt;
        DataBuffer m_skinIndicesExt;

		DataBuffer m_cookedData;
        DataBuffer m_normals;
	};

	struct RESOURCEMESH_API GfxClothChunkData
	{
		RTTI_DECLARE_TYPE(GfxClothChunkData);

		DataBuffer m_positions;
        DataBuffer m_indices;
        DataBuffer m_skinWeights;
        DataBuffer m_skinIndices;
        DataBuffer m_skinWeightsExt;
        DataBuffer m_skinIndicesExt;

		red::DynArray<Uint16> m_simulation{red::PoolResource()};
	};

	struct RESOURCEMESH_API MeshParamCloth : public MeshParameter
	{
		RTTI_DECLARE_TYPE(MeshParamCloth);

        MeshParamCloth();
        ~MeshParamCloth() override;

        MeshParamCloth(const MeshParamCloth&) = delete;
        MeshParamCloth& operator=(MeshParamCloth&) = delete;

		virtual void OnMeshChanged(const CMesh& mesh) override final;

		red::DynArray<PhxClothChunkData> m_chunks{red::PoolResource()};
		red::DynArray<IndexList> m_lodChunkIndices{red::PoolResource()};
        red::DynArray<IndexList> m_drivers{red::PoolResource()};
		THandle<ExportedCapsules> m_capsules;
#ifndef RED_CONFIGURATION_FINAL
        RawClothData m_rawData;
#endif // RED_CONFIGURATION_FINAL
	};

    struct RESOURCEMESH_API MeshParamCloth_Graphical : public MeshParameter
	{
		RTTI_DECLARE_TYPE(MeshParamCloth_Graphical);

        MeshParamCloth_Graphical();
        ~MeshParamCloth_Graphical() override;

        MeshParamCloth_Graphical(const MeshParamCloth_Graphical&) = delete;
        MeshParamCloth_Graphical& operator=(const MeshParamCloth_Graphical&) = delete;

		virtual void OnMeshChanged(const CMesh& mesh) override final;

		red::DynArray<GfxClothChunkData> m_chunks{red::PoolResource()};
		red::DynArray<IndexList> m_lodChunkIndices{red::PoolResource()};
		red::DynArray<ChannelList> m_latchers{red::PoolResource()};
	};

    RESOURCEMESH_API Bool IsValid(THandle<MeshParamCloth_Graphical> param);
    RESOURCEMESH_API Bool IsValid(THandle<MeshParamCloth> param);
	RESOURCEMESH_API Bool IsValid_ForMeshEditor(THandle<MeshParamCloth> param);

    RESOURCEMESH_API red::ArraySpan<const Uint16> ExtractSimulationIndices(const DataViewRO& view, Uint16 chunk);

    RESOURCEMESH_API Uint32 ExtractNumPositions(const DataViewRO& view, Uint16 chunk);
    RESOURCEMESH_API Uint32 ExtractNumChunks(const DataViewRO& view);
	RESOURCEMESH_API Uint32 ExtractNumLODS(const DataViewRO& view);

    RESOURCEMESH_API red::ArraySpan<const Uint16> ExtractLodChunkIndices(const DataViewRO& view, mesh::LODIndex lod);

    RESOURCEMESH_API red::ArraySpan<const Vector3> ExtractPositions(const DataViewRO& view, Uint16 chunk);
    RESOURCEMESH_API red::ArraySpan<const Vector3> ExtractNormals(const DataViewRO& view, Uint16 chunk);
    RESOURCEMESH_API red::ArraySpan<const Uint16>  ExtractIndices(const DataViewRO& view, Uint16 chunk);
    RESOURCEMESH_API red::ArraySpan<const Vector4> ExtractSkinningWeights(const DataViewRO& view, Uint16 chunk);
    RESOURCEMESH_API red::ArraySpan<const Uint32>  ExtractSkinningIndices(const DataViewRO& view, Uint16 chunk);
    RESOURCEMESH_API red::ArraySpan<const Vector4> ExtractSkinningWeightsExt(const DataViewRO& view, Uint16 chunk);
    RESOURCEMESH_API red::ArraySpan<const Uint32>  ExtractSkinningIndicesExt(const DataViewRO& view, Uint16 chunk);

	RESOURCEMESH_API red::ArraySpan<const physics::cloth::ExportedCapsule> ExtractCollisionCapsules(const DataViewRO& view);
}
