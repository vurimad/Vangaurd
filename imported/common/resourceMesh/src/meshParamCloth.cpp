#include "build.h"
#include "meshParamCloth.h"
#include "meshDataViewRO.h"

#include "../../../common/physics/include/physXNatives.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(RawClothData, mesh);
    RTTI_PROPERTY(m_state);
    RTTI_PROPERTY(m_maxDistanceChannel);
	RTTI_PROPERTY(m_maxDistanceExtChannel);
    RTTI_PROPERTY(m_backstopDistanceChannel);
	RTTI_PROPERTY(m_backstopRadiusChannel);
    RTTI_PROPERTY(m_selfCollisionChannel);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(PhxClothChunkData, mesh)
	RTTI_PROPERTY(m_positions);
    RTTI_PROPERTY(m_indices);
    RTTI_PROPERTY(m_skinWeights);
    RTTI_PROPERTY(m_skinIndices);
    RTTI_PROPERTY(m_skinWeightsExt);
    RTTI_PROPERTY(m_skinIndicesExt);
	RTTI_PROPERTY(m_cookedData);
	RTTI_PROPERTY(m_normals);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(GfxClothChunkData, mesh)
	RTTI_PROPERTY(m_positions);
    RTTI_PROPERTY(m_indices);
    RTTI_PROPERTY(m_skinWeights);
    RTTI_PROPERTY(m_skinIndices);
    RTTI_PROPERTY(m_skinWeightsExt);
    RTTI_PROPERTY(m_skinIndicesExt);
	RTTI_PROPERTY(m_simulation);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamCloth, mesh);
RTTI_PARENT_TYPE(mesh::MeshParameter);
	RTTI_PROPERTY(m_lodChunkIndices);
    RTTI_PROPERTY(m_chunks);
    RTTI_PROPERTY(m_drivers);
	RTTI_PROPERTY(m_capsules);
#ifndef RED_CONFIGURATION_FINAL
    RTTI_PROPERTY(m_rawData).notCooked();
#endif // RED_CONFIGURATION_FINAL
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamCloth_Graphical, mesh);
RTTI_PARENT_TYPE(mesh::MeshParameter);
	RTTI_PROPERTY(m_lodChunkIndices);
    RTTI_PROPERTY(m_chunks);
    RTTI_PROPERTY(m_latchers);
RTTI_END_TYPE();

namespace mesh
{
    template<typename T>
    red::ArraySpan<T> MakeSpan(red::BlobView view)
    {
        if( !view.Empty() )
        {
            return red::MakeArraySpan(view.Pointer<T>(), (T*)(view.Pointer<Uint8>() + view.Size()));
        }

        return {};
    }

    RawClothData::RawClothData() = default;

    MeshParamCloth::MeshParamCloth() = default;
    MeshParamCloth::~MeshParamCloth() = default;

	void MeshParamCloth::OnMeshChanged(const CMesh& mesh)
    {}

    MeshParamCloth_Graphical::MeshParamCloth_Graphical() = default;
    MeshParamCloth_Graphical::~MeshParamCloth_Graphical() = default;

	void MeshParamCloth_Graphical::OnMeshChanged(const CMesh& mesh)
    {}

	template< typename T >
	Bool IsValid_ClothChunkData(const T& chunk)
	{
		return !chunk.m_indices.Empty()		&& 
			   !chunk.m_positions.Empty()	&& 
			   !chunk.m_skinIndices.Empty() && 
			   !chunk.m_skinWeights.Empty();
	}

	Bool IsValid_PhxChunk(const PhxClothChunkData& chunk)
	{
		return IsValid_ClothChunkData(chunk) && !chunk.m_normals.Empty() && !chunk.m_cookedData.Empty() && !physics::cloth::NeedsVersionUpdate(chunk.m_cookedData.GetView());
	}

    Bool IsValid(THandle<MeshParamCloth_Graphical> param)
    {
        if( param )
        {
			if( !param->m_lodChunkIndices.Empty() && !param->m_chunks.Empty() )
			{
				return red::alg::AllOf(param->m_chunks, IsValid_ClothChunkData<GfxClothChunkData>);
			}
        }

        return false;
    }

	Bool IsValid(const RawClothData& raw)
	{
		return !raw.m_maxDistanceChannel.Empty() && 
			!raw.m_maxDistanceExtChannel.Empty() &&
			!raw.m_backstopDistanceChannel.Empty() && 
			!raw.m_backstopRadiusChannel.Empty() && 
			!raw.m_selfCollisionChannel.Empty();
	}

    Bool IsValid(THandle<MeshParamCloth> param)
    {
        if( param )
        {
            if( !param->m_lodChunkIndices.Empty() && !param->m_chunks.Empty() )
			{
				return red::alg::AllOf( param->m_chunks, IsValid_PhxChunk);
			}
        }

        return false;
    }

	Bool IsValid_ForMeshEditor(THandle<MeshParamCloth> param)
	{
		if( param )
        {
			return !param->m_lodChunkIndices.Empty() && !param->m_chunks.Empty()
#ifndef RED_CONFIGURATION_FINAL
				&& IsValid(param->m_rawData)
#endif // RED_CONFIGURATION_FINAL
				;
        }

        return false;
	}

    red::ArraySpan<const Uint16> ExtractSimulationIndices(const DataViewRO& view, Uint16 chunk)
    {
        if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            return { param->m_chunks[chunk].m_simulation };
        }

        return {};
    }

    Uint32 ExtractNumPositions(const DataViewRO& view, Uint16 chunk)
    {
        if( auto param = view.GetParameter<MeshParamCloth>() )
        {
            return param->m_chunks[chunk].m_positions.Size() / sizeof(Vector3);
        }
        else if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            return param->m_chunks[chunk].m_positions.Size() / sizeof(Vector3);
        }

        return 0;
    }

    Uint32 ExtractNumChunks(const DataViewRO& view)
    {
        if( auto param = view.GetParameter<MeshParamCloth>() )
        {
			return param->m_chunks.Size();
        }
        else if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            return param->m_chunks.Size();
        }

        return 0;
    }

	Uint32 ExtractNumLODS(const DataViewRO& view)
	{
		if( auto param = view.GetParameter<MeshParamCloth>() )
        {
			return param->m_lodChunkIndices.Size();
        }
        else if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            return param->m_lodChunkIndices.Size();
        }

        return 0;
	}

    red::ArraySpan<const Uint16> ExtractLodChunkIndices(const DataViewRO& view, mesh::LODIndex lod)
    {
        if( auto param = view.GetParameter<MeshParamCloth>() )
        {
			if( lod < param->m_lodChunkIndices.Size() )
			{
				return { param->m_lodChunkIndices[lod] };
			}
        }
        else if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            if( !param->m_lodChunkIndices.Empty() )
            {
                return { param->m_lodChunkIndices[lod] };
            }
        }

        return {};
    }

    red::ArraySpan<const Vector3> ExtractPositions(const DataViewRO& view, Uint16 chunk)
    {
        if( auto param = view.GetParameter<MeshParamCloth>() )
        {
			if( !param->m_chunks[chunk].m_positions.Empty() )
				return MakeSpan<const Vector3>(param->m_chunks[chunk].m_positions.GetView());
        }
        else if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            if( !param->m_chunks[chunk].m_positions.Empty() )
                return MakeSpan<const Vector3>(param->m_chunks[chunk].m_positions.GetView());
        }

        return {};
    }

    red::ArraySpan<const Vector3> ExtractNormals(const DataViewRO& view, Uint16 chunk)
    {
        if( auto param = view.GetParameter<MeshParamCloth>() )
        {
            return MakeSpan<const Vector3>(param->m_chunks[chunk].m_normals.GetView());
        }

        return {};
    }

    red::ArraySpan<const Uint16> ExtractIndices(const DataViewRO& view, Uint16 chunk)
    {
        if( auto param = view.GetParameter<MeshParamCloth>() )
        {
            return MakeSpan<const Uint16>(param->m_chunks[chunk].m_indices.GetView());
        }
        else if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            if( !param->m_chunks[chunk].m_indices.Empty() )
                return MakeSpan<const Uint16>(param->m_chunks[chunk].m_indices.GetView());
        }

        return {};
    }

    red::ArraySpan<const Vector4> ExtractSkinningWeights(const DataViewRO& view, Uint16 chunk)
    {
        if( auto param = view.GetParameter<MeshParamCloth>() )
        {
            return MakeSpan<const Vector4>(param->m_chunks[chunk].m_skinWeights.GetView());
        }
        else if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            if( !param->m_chunks[chunk].m_skinWeights.Empty() )
                return MakeSpan<const Vector4>(param->m_chunks[chunk].m_skinWeights.GetView());
        }

        return {};
    }

    red::ArraySpan<const Uint32> ExtractSkinningIndices(const DataViewRO& view, Uint16 chunk)
    {
        if( auto param = view.GetParameter<MeshParamCloth>() )
        {
            return MakeSpan<const Uint32>(param->m_chunks[chunk].m_skinIndices.GetView());
        }
        else if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            if( !param->m_chunks[chunk].m_skinIndices.Empty() )
                return MakeSpan<const Uint32>(param->m_chunks[chunk].m_skinIndices.GetView());
        }

        return {};
    }

    red::ArraySpan<const Vector4> ExtractSkinningWeightsExt(const DataViewRO& view, Uint16 chunk)
    {
        if( auto param = view.GetParameter<MeshParamCloth>() )
        {
            return MakeSpan<const Vector4>(param->m_chunks[chunk].m_skinWeightsExt.GetView());
        }
        else if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            if( !param->m_chunks[chunk].m_skinWeightsExt.Empty() )
                return MakeSpan<const Vector4>(param->m_chunks[chunk].m_skinWeightsExt.GetView());
        }

        return {};
    }

    red::ArraySpan<const Uint32> ExtractSkinningIndicesExt(const DataViewRO& view, Uint16 chunk)
    {
        if( auto param = view.GetParameter<MeshParamCloth>() )
        {
            return MakeSpan<const Uint32>(param->m_chunks[chunk].m_skinIndicesExt.GetView());
        }
        else if( auto param = view.GetParameter<MeshParamCloth_Graphical>() )
        {
            if( !param->m_chunks[chunk].m_skinIndicesExt.Empty() )
                return MakeSpan<const Uint32>(param->m_chunks[chunk].m_skinIndicesExt.GetView());
        }

        return {};
    }

	red::ArraySpan<const physics::cloth::ExportedCapsule> ExtractCollisionCapsules(const DataViewRO& view)
	{
		if( auto param = view.GetParameter<MeshParamCloth>() )
        {
			if( param->m_capsules )
				return { param->m_capsules->m_capsules };
        }

		return {};
	}
}