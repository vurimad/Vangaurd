/**
* Copyright (c) 2008 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshDataView.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/redContainers/include/hashSet.h"

#if !defined( VANGUARD_ASSET_HOST )
#include "materialLayersLoader.h"
#endif
#include "meshRenderBlobUtils.h"
#include "../../../common/redReflection/include/util.h"
#include "../../../common/resourceMesh/include/meshPacker.h"

#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMesh/include/meshUtils.h"

//#pragma optimize("",off)

namespace
{
	void ReindexMaterialEntriesAfterMaterialRemoval( red::ArraySpan< CMesh::MaterialEntry > materialEntries,
	                                                 Bool removedMaterialWasLocalInstance, Uint16 indexOfRemovedMaterialOnInternalArray )
	{
		// Reindex material entries after removing element from one of the internal material arrays.
		for ( CMesh::MaterialEntry& materialEntry : materialEntries )
		{
			if ( (materialEntry.IsLocalInstance() == removedMaterialWasLocalInstance) &&
			     (materialEntry.GetIndex() >= indexOfRemovedMaterialOnInternalArray) )
			{
				const Uint16 prevIndex = materialEntry.GetIndex();
				materialEntry.SetIndex(prevIndex - 1);
			}
		}
	}
}

namespace mesh
{

	DataView::DataView(CMesh& mesh)
		: DataViewRO(mesh)
		, m_mesh(&mesh)
		, m_recreateBlob(false)
		, m_recreateRenderResource(false)
		, m_recreateMaterialSetup(false)
		, m_recalculateGeometryHash(false)
	{}

	DataView::~DataView()
	{
		Apply();
	}

	void DataView::Apply( bool skipResourceCreation )
	{
		Uint32 flags = 0;

		if (m_recreateBlob)
		{
			if ( !tools::CreateMeshRenderResourceBlob( *m_mesh, PLATFORM_PC, true ) )
			{
				RED_LOG_ERROR( "Could not create mesh render resource blob" );
			}
			else // if we succesfully recreated blob, the render resource is destroyed and needs to be recreated
			{
				m_recreateRenderResource = true;
			}
		}

		if (m_recreateMaterialSetup)
			flags |= CMesh::MeshInternalData_RenderMaterialSetup;

		if (m_recreateRenderResource)
			flags |= CMesh::MeshInternalData_RenderResource;

		if( flags && !skipResourceCreation )
		{
			m_mesh->RecreateInternalResources( flags, true );
		}
		
		if ( m_recalculateGeometryHash )
			m_mesh->m_geometryHash = CalculateGeometryHash();

		// flag reset to avoid redundant resources recreation
		m_recreateBlob = false;
		m_recreateMaterialSetup = false;
		m_recreateRenderResource = false;
		m_recalculateGeometryHash = false;
	}

	void DataView::SetLODDistance(LODIndex lodIndex, Float dist)
	{
		if (lodIndex >= m_mesh->m_lodLevelInfo.Size())
			return;

		Float minLodDist = 0.0f;
		Float maxLodDist = m_mesh->GetAutoHideDistance();

		if (lodIndex > 0)
			minLodDist = std::max<Float>(minLodDist, GetLODDistance(lodIndex - 1));

		if (lodIndex < (GetNumLODs() - 1))
			maxLodDist = std::min<Float>(maxLodDist, GetLODDistance(lodIndex + 1));

		const Float safeDist = math::Clamp<Float>(dist, minLodDist, maxLodDist);
		if (safeDist != m_mesh->m_lodLevelInfo[lodIndex])
		{
			m_mesh->m_lodLevelInfo[lodIndex] = safeDist;
			m_recreateBlob = true;
		}
	}

	Bool DataView::RemoveLODWithData( LODIndex lodIndex, RemoveLODMode mode )
	{
		red::DynArray< Uint16 > chunks = GetLODChunkIndices( lodIndex );
		std::sort( chunks.Begin(), chunks.End() );

		if ( !RemoveLOD( lodIndex ) )
		{
			return false;
		}

		for ( const Uint16 chunkIndex : chunks.Reverse() )
		{
			if ( GetChunkLodMask( chunkIndex ) != 0 )
			{
				continue;
			}

			if ( mode == RemoveLODMode::RemoveUnusedChunks )
			{
				m_mesh->m_chunks.RemoveAt( chunkIndex );

				for ( const THandle< MeshAppearance >& app : m_mesh->m_appearances )
				{
					app->m_chunkMaterials.RemoveAt( chunkIndex );
				}
			}
			else if ( mode == RemoveLODMode::ClearUnusedChunks )
			{
				SMeshChunkPacked& chunk = m_mesh->m_chunks[chunkIndex];
				chunk.m_numVertices = 0;
				chunk.m_numIndices = 0;
				chunk.m_streamMask = 0;
				chunk.m_streams.Clear();
			}
		}
		m_recreateMaterialSetup = true;
		m_recalculateGeometryHash = true;
		m_recreateBlob = true;
		return true;
	}

	Bool DataView::RemoveLOD( LODIndex lodIndex )
	{
		if ( GetNumLODs() <= 1 || lodIndex >= m_mesh->m_lodLevelInfo.Size() )
			return false;

		Uint8 newLodMask = 0;
		for ( LODIndex i = lodIndex; i < m_mesh->m_lodLevelInfo.Size(); ++i )
		{
			const Uint8 lodMask = 1 << i;
			for ( auto& chunkInfo : m_mesh->m_chunks )
			{
				if ( chunkInfo.m_lodMask == lodMask )
				{
					chunkInfo.m_lodMask = newLodMask;
				}
			}
			newLodMask = lodMask;
		}
		m_mesh->m_lodLevelInfo.RemoveAt( lodIndex );

		m_recreateBlob = true;
		m_recreateRenderResource = true;
		return true;
	}

	void DataView::SetChunkVertexType( ChunkIndex chunkIndex, EMeshVertexType newVertexType )
	{
		if ( chunkIndex >= m_mesh->m_chunks.Size() )
			return;

		auto& chunk = m_mesh->m_chunks[chunkIndex];
		m_recalculateGeometryHash = m_recreateBlob = (chunk.m_vertexType != newVertexType);
		chunk.m_vertexType = newVertexType;
	}

	void DataView::SetChunkLodMask(ChunkIndex chunkIndex, Uint8 lodMask)
	{
		if (chunkIndex >= m_mesh->m_chunks.Size())
			return;

		// TODO!!! per-chunk LOD mask vs chunk list in LOD, this is a big change
	}

	void DataView::SetChunkMaterialName(ChunkIndex chunkIndex, ChunkMaterialIndex index, CName materialName)
	{
		if (chunkIndex >= m_mesh->m_chunks.Size())
			return;

		auto& chunk = m_mesh->m_chunks[chunkIndex];

		if (index <= chunk.m_materialID.Size())
		{
			if (chunk.m_materialID[index] != materialName)
			{
				chunk.m_materialID[index] = materialName;
				m_recreateBlob = true;
			}
		}
	}

	void DataView::SetChunkFlags(ChunkIndex chunkIndex, ChunkFlags flags)
	{
		if (chunkIndex >= m_mesh->m_chunks.Size())
			return;

		auto& chunk = m_mesh->m_chunks[chunkIndex];

		EMeshChunkFlags newRenderMask = EMeshChunkFlags(0);
		if ( flags.m_renderInScene )		*(Uint16*)&newRenderMask |= MCF_RenderInScene;
		if ( flags.m_renderInShadows )		*(Uint16*)&newRenderMask |= MCF_RenderInShadows;
		if ( flags.m_isTwoSided )			*(Uint16*)&newRenderMask |= MCF_IsTwoSided;
		if ( flags.m_isRayTracedEmissive )	*(Uint16*)&newRenderMask |= MCF_IsRayTracedEmissive;

		if ( chunk.m_chunkRenderMask != newRenderMask )
		{
			m_recreateBlob = true;
			m_recalculateGeometryHash = (((chunk.m_chunkRenderMask ^ newRenderMask) & MCF_IsTwoSided) != 0);
			chunk.m_chunkRenderMask = newRenderMask;
		}
	}

	void DataView::SetChunkData(ChunkIndex chunkIndex, ChunkStreamDataRO data)
	{
		if (chunkIndex >= m_mesh->m_chunks.Size())
			return;

		auto& chunk = m_mesh->m_chunks[chunkIndex];
		chunk.m_streamMask |= data.m_stream;

		SMeshStream* stream = nullptr;

		for (auto& currentStream : chunk.m_streams)
		{
			if (currentStream.m_type == data.m_stream)
			{
				stream = &currentStream;
				break;
			}
		}

		Uint32 size = data.m_count * data.m_stride;

		if (!stream)
		{
			stream = &chunk.m_streams.EmplaceBack();
			stream->m_type = data.m_stream;
			chunk.m_streamMask |= stream->m_type;

			if ( stream->m_type == MST_SkinningIndices_4U8 || stream->m_type == MST_SkinningWeights_4F )
			{
				chunk.m_vertexType = MVT_SkinnedMesh;
			}
			m_recreateRenderResource = true;
		}
		m_recreateBlob = true;
		m_recalculateGeometryHash = true;
		stream->m_data.Editor_SetContentDirect( data.m_data, size );
	}

	void DataView::SetChunkSourceMeshUUID( ChunkIndex chunkIndex, const String& uuid )
	{
		if ( chunkIndex >= m_mesh->m_chunks.Size() )
			return;

		m_mesh->m_chunks[chunkIndex].m_sourceMeshUUID = uuid;
	}

	void DataView::SetChunkSourceMaterialUUID( ChunkIndex chunkIndex, const String& uuid )
	{
		if ( chunkIndex >= m_mesh->m_chunks.Size() )
			return;

		m_mesh->m_chunks[chunkIndex].m_sourceMaterialUUID = uuid;
	}

	void DataView::RemoveChunkStream( ChunkIndex chunkIndex, EMeshStreamType streamType, Bool recreateBlob )
	{
		RemoveChunkStreams( chunkIndex, streamType, recreateBlob );
	}

	void DataView::RemoveChunkStreams( ChunkIndex chunkIndex, Uint32 streamsMask, Bool recreateBlob )
	{
		if ( chunkIndex >= m_mesh->m_chunks.Size() )
			return;

		SMeshChunkPacked& chunk = m_mesh->m_chunks[chunkIndex];

		Uint64 newStreamMask = 0;
		red::DynArray< SMeshStream > remainingStreams;
		remainingStreams.Reserve( chunk.m_streams.Size() );

		for ( SMeshStream& chunkStream : chunk.m_streams )
		{
			if ( (chunkStream.m_type & streamsMask) == 0 )
			{
				remainingStreams.PushBack( std::move( chunkStream ) );
				newStreamMask |= chunkStream.m_type;
			}
		}

		chunk.m_streams.Swap( remainingStreams );
		chunk.m_streamMask = newStreamMask;

		m_recreateBlob = recreateBlob;
		m_recalculateGeometryHash = recreateBlob;
	}

	void DataView::RemoveMeshStreams( Uint32 streamsMask, Bool recreateBlob )
	{
		for ( ChunkIndex chunkIndex = 0; chunkIndex < GetNumChunks(); ++chunkIndex )
		{
			RemoveChunkStreams( chunkIndex, streamsMask, recreateBlob );
		}
	}

	void DataView::SetMaterialData(MaterialIndex index, THandle<IMaterial> newMaterialData)
	{
		auto& lock = m_meshRO->m_materialEntriesLock;
		RED_SCOPE_LOCK(lock);

		if (index >= m_mesh->m_materialEntries.Size())
			return;

		CMesh::MaterialEntry& materialEntry = m_mesh->m_materialEntries[index];

		Uint16 indexOnInternalArray = materialEntry.GetIndex();
		Bool oldMaterialIsInstance = materialEntry.IsLocalInstance();
		Bool newMaterialIsInstance = IsMaterialLocalInstance( newMaterialData.Get() );

		materialEntry.SetMaterial( newMaterialData );
		materialEntry.SetIsLocalInstance( newMaterialIsInstance );

		if ( newMaterialIsInstance == oldMaterialIsInstance )
		{
			// Just update the pointer, the asset/instance type of the material did not change.
			if ( newMaterialIsInstance )
			{
				m_mesh->m_localMaterialBuffer.Editor_InsertAt( indexOnInternalArray, Cast<CMaterialInstance>( newMaterialData ) );
			}
			else
			{
				RED_FATAL_ASSERT(newMaterialData->GetPath().IsValid());
				m_mesh->m_externalMaterials[indexOnInternalArray] = newMaterialData->GetPath();
			}
		}
		else
		{
			// The material changed from asset to instance or vice versa, we need to correctly update both internal arrays.
			if ( oldMaterialIsInstance )
			{
				m_mesh->m_localMaterialBuffer.Editor_RemoveAt( indexOnInternalArray );
			}
			else
			{
				m_mesh->m_externalMaterials.RemoveAt( indexOnInternalArray );
			}

			ReindexMaterialEntriesAfterMaterialRemoval( m_mesh->m_materialEntries, oldMaterialIsInstance, indexOnInternalArray );
			CMesh::InsertMaterialToInternalArray( m_mesh->m_externalMaterials, m_mesh->m_localMaterialBuffer, materialEntry );
		}

		m_recreateMaterialSetup = true;
	}

	void DataView::SetMaterialName(MaterialIndex index, CName newName)
	{
		auto& lock = m_meshRO->m_materialEntriesLock;
		RED_SCOPE_LOCK(lock);

		if (index >= m_mesh->m_materialEntries.Size())
			return;

		if (m_mesh->m_materialEntries[index].GetName() == newName )
			return;

		// material names must be unique
		const auto numMaterials = GetNumMaterials();
		for (Uint32 i = 0; i < numMaterials; ++i)
		{
			if (i != index && GetMaterialName_NoLock(i) == newName )
				return;
		}

		// set new material name
		CName oldName = m_mesh->m_materialEntries[index].GetName();
		m_mesh->m_materialEntries[index].SetName( newName );

		// change the chunk mapping
		const auto numChunks = GetNumChunks();					/// @todo DP: this should be removed when we remove material names from chunk
		for (Uint32 i = 0; i < numChunks; ++i)
		{
			const auto numChunkMaterials = GetChunkMaterialCount(i);
			for (Uint32 j = 0; j < numChunkMaterials; ++j)
			{
				const auto chunkMaterialName = GetChunkMaterialName(i, j);
				if (chunkMaterialName == oldName)
				{
					SetChunkMaterialName(i, j, newName );
				}
			}
		}

		// correct mesh appearances
		for ( Uint32 appearanceIndex = 0; appearanceIndex < GetNumAppearances(); ++appearanceIndex )
		{
			THandle< mesh::MeshAppearance > appearance = GetAppearanceData( appearanceIndex );
			for ( CName& chunkMaterialName : appearance->m_chunkMaterials )
			{
				if ( chunkMaterialName == oldName )
				{
					chunkMaterialName = newName;
				}
			}
		}

		m_recreateMaterialSetup = true;
		m_recreateBlob = true;
	}

	//------

	Bool DataView::RemoveUnusedBones( red::DynArray<Uint32>& boneReindex )
	{
		red::StaticArray<Uint32, 9> skinningHistogram;
		red::DynArray<Uint32> boneUsageHistogram;

		Uint32 numBones = GetNumBones();

		boneUsageHistogram.Resize( numBones );
		for ( Uint32 i = 0; i < numBones; ++i )
			boneUsageHistogram[i] = 0;

		for ( Uint32 chunkIndex = 0; chunkIndex < GetNumChunks(); ++chunkIndex )
			CalcChunkSkinningHistogram( chunkIndex, skinningHistogram, &boneUsageHistogram );

		CMesh::TSkeletonBoneNameArray newBoneNames;
		CMesh::TSkeletonBoneMatrixArray newBoneRigMatrices;
		CMesh::TSkeletonBoneEpsilonArray newBoneVertexEpsilons;
		boneReindex.Resize( numBones );

		for ( Uint32 boneIndex = 0; boneIndex < numBones; ++boneIndex )
		{
			boneReindex[boneIndex] = newBoneNames.Size();

			if ( boneUsageHistogram[boneIndex] > 0 )
			{
				newBoneNames.PushBack( m_meshRO->m_boneNames[boneIndex] );
				newBoneRigMatrices.PushBack( m_meshRO->m_boneRigMatrices[boneIndex] );
				newBoneVertexEpsilons.PushBack( m_meshRO->m_boneVertexEpsilons[boneIndex] );
			}
		}

		if ( newBoneNames.Size() == m_meshRO->m_boneNames.Size() )       // No bones were removed.
			return false;

		m_mesh->m_boneNames.Swap( newBoneNames );
		m_mesh->m_boneRigMatrices.Swap( newBoneRigMatrices );
		m_mesh->m_boneVertexEpsilons.Swap( newBoneVertexEpsilons );

		ReindexVertexBoneIndices( boneReindex );                         // Correct vertex bone indices.
		return true;
	}

	Bool DataView::RemoveUnusedBones()
	{
		red::DynArray<Uint32> boneReindex;
		return RemoveUnusedBones( boneReindex );
	}

	void DataView::ReindexVertexBoneIndices( const red::DynArray<Uint32>& boneReindex )
	{
		for ( Uint32 chunkIndex = 0; chunkIndex < GetNumChunks(); ++chunkIndex )
		{
			Uint32 numVertices = GetChunkVertexCount( chunkIndex );
			ChunkStreamDataRO vertexIndicesStream = GetChunkStreamData( chunkIndex, MST_SkinningIndices_4U8 );
			ChunkStreamDataRO vertexIndicesExtStream = GetChunkStreamData( chunkIndex, MST_SkinningIndicesExt_4U8 );
			Uint8* vertexIndicesData = reinterpret_cast<Uint8*>(const_cast<void*>(vertexIndicesStream.m_data));		/// @todo DP: remove const_cast, find better solution
			Uint8* vertexIndicesExtData = reinterpret_cast<Uint8*>(const_cast<void*>(vertexIndicesExtStream.m_data));

			for ( Uint32 vertexIndex = 0; vertexIndex < numVertices; ++vertexIndex )
			{
				if ( vertexIndicesData != nullptr )
				{
					for ( Uint32 i = 0; i < 4u; ++i )
					{
						Uint32 oldBoneIndex = vertexIndicesData[i];
						vertexIndicesData[i] = static_cast<Uint8>(boneReindex[oldBoneIndex]);
					}
				}
				if ( vertexIndicesExtData != nullptr )
				{
					for ( Uint32 i = 0; i < 4u; ++i )
					{
						Uint32 oldBoneIndex = vertexIndicesExtData[i];
						vertexIndicesExtData[i] = static_cast<Uint8>(boneReindex[oldBoneIndex]);
					}
				}

				vertexIndicesData += vertexIndicesStream.m_stride;
				vertexIndicesExtData += vertexIndicesExtStream.m_stride;
			}
		}

		m_recalculateGeometryHash = true;
	}

	void DataView::InvertBoneReindex( red::DynArray<Uint32>& boneReindex )
	{
		red::DynArray<Uint32> newBoneReindex( boneReindex.Size() );
		for ( Uint32 oldIndex = 0; oldIndex < boneReindex.Size(); ++oldIndex )
		{
			Uint32 newIndex = boneReindex[oldIndex];
			newBoneReindex[newIndex] = oldIndex;
		}
		boneReindex.Swap( newBoneReindex );
	}

	void DataView::ResetBones( const red::DynArray< CName >& newBonesNames,
	                           const red::DynArray< Matrix >& newBoneRigMatrices,
	                           const red::DynArray< Float >& newBoneVertexEpsilons )
	{
		RED_FATAL_ASSERT( (newBonesNames.Size() == newBoneRigMatrices.Size()) && (newBonesNames.Size() == newBoneVertexEpsilons.Size()) );
		m_mesh->m_boneNames = newBonesNames;
		m_mesh->m_boneRigMatrices = newBoneRigMatrices;
		m_mesh->m_boneVertexEpsilons = newBoneVertexEpsilons;
	}

	//------

	void DataView::RemoveMaterials()
	{
		auto& lock = m_meshRO->m_materialEntriesLock;
		RED_SCOPE_LOCK(lock);

		m_mesh->m_materialEntries.Clear();
		m_mesh->m_externalMaterials.Clear();
		m_mesh->m_localMaterialBuffer.Editor_Clear();

		m_recreateMaterialSetup = true;
	}

	void DataView::RemoveUnusedMaterials( red::DynArray<Uint32>* removedMaterialIndices, Bool previewInsteadOfRemove )
	{
		// Gather all used materials, from any chunks.
		red::HashSet< CName > usedMaterialNames;

		for ( Uint32 appearanceIndex = 0; appearanceIndex < GetNumAppearances(); appearanceIndex++ )
		{
			Uint32 numChunksInAppearance = GetAppearanceChunkCount( appearanceIndex );        // This should be the same for all appearances of a given mesh.
			for ( Uint32 chunkIndex = 0; chunkIndex < numChunksInAppearance; ++chunkIndex )
			{
				ChunkMaterialIndex numMaterialsInChunk = GetAppearanceChunkMaterialCount( appearanceIndex, chunkIndex );   // More than 1 for multilayer, typically just 1.
				for ( ChunkMaterialIndex materialIndex = 0; materialIndex < numMaterialsInChunk; ++materialIndex )
				{
					CName materialName = GetAppearanceMaterialName( appearanceIndex, chunkIndex, materialIndex );
					usedMaterialNames.Insert( materialName );
				}
			}
		}

		// Remove unused materials.
		red::DynArray< CMesh::MaterialEntry > newMaterialEntries;       // New list of materials, after removal.
		red::DynArray< TResAsyncRef<IMaterial> > newExternalMaterials;
		mesh::MeshMaterialBuffer newLocalMaterialBuffer;

		MaterialIndex numMaterials = GetNumMaterials();
		for ( MaterialIndex materialIndex = 0; materialIndex < numMaterials; ++materialIndex )
		{
			CName materialName = GetMaterialName( materialIndex );
			if ( usedMaterialNames.Exist( materialName ) )
			{
				CMesh::MaterialEntry materialEntry;
				materialEntry.SetName( materialName );
				materialEntry.SetMaterial( GetMaterialData( materialIndex ) );
				materialEntry.SetIsLocalInstance( IsMaterialLocalInstance( materialIndex ) );
				CMesh::InsertMaterialToInternalArray( newExternalMaterials, newLocalMaterialBuffer, materialEntry );
				newMaterialEntries.PushBack( materialEntry );
			}
			else if ( removedMaterialIndices != nullptr )
			{
				removedMaterialIndices->PushBack( materialIndex );
			}
		}

		if( !previewInsteadOfRemove )
		{
			m_mesh->m_materialEntries = std::move( newMaterialEntries );
			m_mesh->m_externalMaterials = std::move( newExternalMaterials );
			m_mesh->m_localMaterialBuffer.Editor_CopyAssign( newLocalMaterialBuffer );

			m_recreateMaterialSetup = true;
		}
	}

	Bool DataView::ResetMaterials( const red::DynArray< THandle< IMaterial > >& newMaterials, const red::DynArray< CName >& newMaterialNames )
	{
		RED_FATAL_ASSERT( newMaterials.Size() == newMaterialNames.Size() );
		RED_FATAL_ASSERT( !newMaterials.Empty() );

		red::HashSet< CName > newMaterialsSet;
		for ( const CName& materialName : newMaterialNames )
			newMaterialsSet.Insert( materialName );

		for ( Uint32 appearanceIndex = 0; appearanceIndex < GetNumAppearances(); appearanceIndex++ )
		{
			Uint32 numChunksInAppearance = GetAppearanceChunkCount( appearanceIndex );        // This should be the same for all appearances of a given mesh.
			for ( Uint32 chunkIndex = 0; chunkIndex < numChunksInAppearance; ++chunkIndex )
			{
				ChunkMaterialIndex numMaterialsInChunk = GetAppearanceChunkMaterialCount( appearanceIndex, chunkIndex );   // More than 1 for multilayer, typically just 1.
				for ( ChunkMaterialIndex materialIndex = 0; materialIndex < numMaterialsInChunk; ++materialIndex )
				{
					CName materialName = GetAppearanceMaterialName( appearanceIndex, chunkIndex, materialIndex );
					if ( !newMaterialsSet.Exist( materialName ) )
						return false;                                     // There is no such material in new materials set.
				}
			}
		}

		red::DynArray< CMesh::MaterialEntry > newMaterialEntries;
		red::DynArray< TResAsyncRef<IMaterial> > newExternalMaterials;
		mesh::MeshMaterialBuffer newLocalMaterialBuffer;

		for ( Uint32 i = 0; i < newMaterials.Size(); ++i )
		{
			CMesh::MaterialEntry newMaterialEntry;
			newMaterialEntry.SetName( newMaterialNames[i] );
			newMaterialEntry.SetMaterial( newMaterials[i] );
			newMaterialEntry.SetIsLocalInstance( IsMaterialLocalInstance( newMaterials[i].Get() ) );
			CMesh::InsertMaterialToInternalArray( newExternalMaterials, newLocalMaterialBuffer, newMaterialEntry );
			newMaterialEntries.PushBack( newMaterialEntry );
		}

		m_mesh->m_materialEntries = std::move( newMaterialEntries );
		m_mesh->m_externalMaterials = std::move( newExternalMaterials );
		m_mesh->m_localMaterialBuffer.Editor_CopyAssign( newLocalMaterialBuffer );
		return true;
	}

	String DataView::GenerateUniqueMaterialName( const String& initialMaterialName ) const
	{
		Uint32 largestIndex = 0;
		Bool collisionsFound = false;

		for ( Uint32 i = 0; i < GetNumMaterials(); ++i )
		{
			String materialName = GetMaterialName( i ).AsChar();
			if ( materialName.BeginsWith( initialMaterialName ) )
			{
				if ( materialName == initialMaterialName )
				{
					collisionsFound = true;
				}
				else
				{
					String materialNameSuffix = materialName.StringAfter( initialMaterialName );
					materialNameSuffix.TrimLeft( '_' );
					Uint32 suffixIndex = 0;
					if ( FromString( materialNameSuffix, suffixIndex ) )
					{
						largestIndex = Max( suffixIndex, largestIndex );
						collisionsFound = true;
					}
				}
			}
		}

		if ( !collisionsFound )
			return initialMaterialName;
		else
			return String::Printf( "%hs_%u", initialMaterialName.AsChar(), largestIndex + 1u );
	}

	const Bool DataView::AddMaterial( THandle<IMaterial> materialData, CName name, Int32 index /*=-1*/, Bool overridePreviousVersion /*=true*/ )
	{
		// make sure name is not used
		const auto numMaterials = GetNumMaterials();
		for ( Uint32 i = 0; i < numMaterials; ++i )								/// @todo DP: simplify this
		{
			if ( GetMaterialName( i ) == name )
			{
				if ( overridePreviousVersion )
				{
					SetMaterialData( i, materialData );
					m_recreateMaterialSetup = true;
				}

				return false;
			}
		}

		// Add new material.
		CMesh::MaterialEntry newMaterialEntry;
		newMaterialEntry.SetName( name );
		newMaterialEntry.SetMaterial( materialData );
		newMaterialEntry.SetIsLocalInstance( IsMaterialLocalInstance( materialData.Get() ) );
		CMesh::InsertMaterialToInternalArray( m_mesh->m_externalMaterials, m_mesh->m_localMaterialBuffer, newMaterialEntry );

		if ( index < 0 )
		{
			m_mesh->m_materialEntries.PushBack( newMaterialEntry );         // append at the end of the list
		}
		else
		{
			m_mesh->m_materialEntries.InsertAt( index, newMaterialEntry );  // insert at given index
		}

		m_recreateMaterialSetup = true;
		return true;
	}

	const Bool DataView::RemoveMaterial(MaterialIndex index)
	{
		// get material name
		const auto materialName = GetMaterialName(index);

		// make sure appearances do not use this material
		for ( Uint32 appearanceIndex = 0; appearanceIndex < GetNumAppearances(); ++appearanceIndex )
		{
			THandle< mesh::MeshAppearance > appearance = GetAppearanceData( appearanceIndex );
			for ( const CName& chunkMaterialName : appearance->m_chunkMaterials )
			{
				if ( chunkMaterialName == materialName )
				{
					return false; // material in use
				}
			}
		}

		// delete the material from list
		Bool isLocalInstance = IsMaterialLocalInstance( index );
		Uint32 indexOnInternalArray = m_mesh->m_materialEntries[index].GetIndex();
		if ( isLocalInstance )
		{
			m_mesh->m_localMaterialBuffer.Editor_RemoveAt( indexOnInternalArray );
		}
		else
		{
			m_mesh->m_externalMaterials.RemoveAt( indexOnInternalArray );
		}

		m_mesh->m_materialEntries.RemoveAt( index );

		ReindexMaterialEntriesAfterMaterialRemoval( m_mesh->m_materialEntries, isLocalInstance, indexOnInternalArray );

		m_recreateMaterialSetup = true;
		return true;
	}

	//------

	void DataView::RemoveChunkMaterials(const ChunkIndex chunkIndex)
	{
		// invalid chunk index
		if (chunkIndex >= GetNumChunks())
			return;

		m_mesh->m_chunks[chunkIndex].m_materialID.Clear();
		m_recreateBlob = true;
	}

	const Bool DataView::AddChunkMaterial(const ChunkIndex chunkIndex, CName name)
	{
		// invalid chunk index
		if (chunkIndex >= GetNumChunks())
			return false;

		// make sure name is used
		Bool materialExists = false;
		const auto numMaterials = GetNumMaterials();
		for (Uint32 i = 0; i < numMaterials; ++i)
		{
			if (GetMaterialName(i) == name)
			{
				materialExists = true;
				break;
			}
		}

		// we cannot set material that does not exist
		if (!materialExists)
			return false;

		// add material to chunk
		m_mesh->m_chunks[chunkIndex].m_materialID.PushBack(name);
		m_recreateBlob = true;

		return true;
	}

	const Bool DataView::RemoveChunkMaterial(const ChunkIndex chunkIndex, CName name)
	{
		// invalid chunk index
		if (chunkIndex >= GetNumChunks())
			return false;

		// remove
		if (!m_mesh->m_chunks[chunkIndex].m_materialID.Remove(name).IsSuccessful())
			return false;

		m_recreateBlob = true;
		return true;
	}

	const Bool DataView::SetChunkMaterial( CName materialName, const ChunkIndex chunkIndex, const Uint32 appearanceIndex )
	{
		// invalid index
		if (chunkIndex >= GetNumChunks() || appearanceIndex >= GetNumAppearances())
			return false;

		// make sure name is used
		Bool materialExists = false;
		const auto numMaterials = GetNumMaterials();
		for (Uint32 i = 0; i < numMaterials; ++i)
		{
			if (GetMaterialName( i ) == materialName)
			{
				materialExists = true;
				break;
			}
		}

		// we cannot set material that does not exist
		if (!materialExists)
			return false;

		// substitute material on specific chunk
		m_mesh->m_appearances[appearanceIndex]->m_chunkMaterials[ chunkIndex ] = materialName;
		
		m_recreateBlob = true;
		return true;
	}

	//------

	const Bool DataView::ReplaceDefaultAppearance( THandle< mesh::MeshAppearance > appearance )
	{
		Uint32 appIndex = GetAppearanceIndex( appearance->GetName() );
		CName appName = appearance->GetName();
		if( appIndex != -1 && appName != MeshAppearance::c_DefaultAppearanceName && !appName.Empty() )
			return false;
		if( m_mesh->m_appearances.Size() > appIndex )
			m_mesh->m_appearances.RemoveAt( appIndex );

		RED_FATAL_ASSERT( IsAppearanceCompatible( *appearance ), "Attempt to add appearance to a wrong mesh (this appearance has invalid number of chunks)" );

		m_mesh->m_appearances.InsertAt( 0, appearance );
		m_mesh->m_appearances[ 0 ]->m_meshOwner = m_mesh;
		m_mesh->m_appearances[ 0 ]->ForceReloadRenderMaterialSetup( false, false );        // We are editing, it's fine to create render material right now.
		return true;
	}

	const Bool DataView::IsAppearanceCompatible( const mesh::MeshAppearance& apperance ) const
	{
		return GetNumChunks() == apperance.GetNumChunks();
	}

	const Bool DataView::SetAppearanceTags( Int32 appearanceIndex, const red::DynArray< CName >& newTags )
	{
		if ( (appearanceIndex < 0 ) || (static_cast<Uint32>(appearanceIndex) >= m_mesh->m_appearances.Size()) )
			return false;

		const THandle< mesh::MeshAppearance > appearance = m_mesh->m_appearances[appearanceIndex];
		appearance->m_tags = newTags;
		return true;
	}

	const Bool DataView::SetAppearanceChunkMaterial( Int32 appearanceIndex, Int32 chunkIndex, CName materialName )
	{
		if ( (appearanceIndex < 0) || (static_cast<Uint32>(appearanceIndex) >= m_mesh->m_appearances.Size()) )
			return false;

		const THandle< mesh::MeshAppearance > appearance = m_mesh->m_appearances[appearanceIndex];
		if ( (chunkIndex < 0) || (static_cast<Uint32>(chunkIndex) >= appearance->m_chunkMaterials.Size()) )
			return false;

		appearance->m_chunkMaterials[ chunkIndex ] = materialName;
		m_recreateRenderResource = true;
		m_recreateMaterialSetup = true;
		m_recreateBlob = true;
		return true;
	}

	const Bool DataView::AddAppearance( CName name, Int32 index )
	{
		THandle< MeshAppearance > defaultAppearance = GetDefaultAppearanceData();
		return AddAppearanceCopy( name, *defaultAppearance, index );
	}

	const Bool DataView::AddAppearance( THandle< mesh::MeshAppearance > appearance, Int32 index )
	{
		if ( GetAppearanceIndex( appearance->GetName() ) != -1 )       // Appearance names must be unique.
			return false;

		if ( index < 0 )
			index = m_mesh->m_appearances.Size();

		RED_FATAL_ASSERT( appearance->GetMesh() == m_mesh, "Attempt to add appearance to a wrong mesh (this appearance comes from another mesh)" );
		m_mesh->m_appearances.InsertAt( index, appearance );
		return true;
	}

	const Bool DataView::AddAppearanceCopy( CName name, const mesh::MeshAppearance& sourceAppearance, Int32 index )
	{
		// make sure name is not used
		Int32 exisitingAppearanceIndex = GetAppearanceIndex( name );
		if ( name == MeshAppearance::c_DefaultAppearanceName && exisitingAppearanceIndex >= 0 )
		{
			if ( GetAppearanceName( exisitingAppearanceIndex ) != name )
			{
				exisitingAppearanceIndex = -1;
			}
		}

		if ( exisitingAppearanceIndex != -1 )       // Appearance names must be unique.
			return false;

		if ( index < 0 )
			index = m_mesh->m_appearances.Size();

		RED_FATAL_ASSERT( IsAppearanceCompatible( sourceAppearance ), "Attempt to add appearance to a wrong mesh (this appearance has invalid number of chunks)" );

		THandle< MeshAppearance > appearance( CreateHandle< MeshAppearance >( sourceAppearance, name ) );
		appearance->m_meshOwner = m_mesh;
		appearance->ForceReloadRenderMaterialSetup( false, false );        // We are editing, it's fine to create render material right now.
		m_mesh->m_appearances.InsertAt( index, appearance );
		return true;
	}

	const Bool DataView::RemoveAppearance( CName name )
	{
		if ( m_mesh->m_appearances.Size() == 1u )   // We need to have always at least one appearance present.
			return false;

		for ( CMesh::TAppearances::iterator appearance = m_mesh->m_appearances.Begin(); appearance != m_mesh->m_appearances.End(); ++appearance )
		{
			if ( (*appearance)->GetName() == name )
			{
				m_mesh->m_appearances.Remove( appearance );
				return true;
			}
		}
		return false;
	}

	void DataView::RemoveAppearances()
	{
		m_mesh->m_appearances.Clear();

		m_recreateMaterialSetup = true;
	}

	const Bool DataView::RenameAppearance( Int32 index, CName newName )
	{
		if ( GetAppearanceIndex( newName ) != -1 )       // Appearance names must be unique.
		{
			if ( !( newName == MeshAppearance::c_DefaultAppearanceName ) )
				return false;

			// We need a special case for rename to a "default" name. GetAppearanceIndex will always find an appearance named "default".
			// We need to test if the default appearance is actually named "default".
			if ( m_meshRO->m_appearances[index]->GetName() == newName )
				return false;
		}

		if ( static_cast<Uint32>(index) >= m_mesh->m_appearances.Size() )
			return false;

		m_mesh->m_appearances[index]->m_name = newName;
		return true;
	}

	//------

	Bool DataView::ReimportLayersFromJSON( CName appearanceName, ChunkIndex chunkIndex, const red::String& jsonFilePath )
	{
#if defined( VANGUARD_ASSET_HOST )
		RED_UNUSED( appearanceName );
		RED_UNUSED( chunkIndex );
		RED_UNUSED( jsonFilePath );
		return false;
#else
		MaterialLayerHelpers::TMeshLayerDefinitionArray layeredSetup;
		if ( MaterialLayerHelpers::Load( red::AbsolutePath::CreateFilePath( jsonFilePath ), layeredSetup ) )
		{
			Int32 appearanceIndex = GetAppearanceIndex( appearanceName );
			if ( appearanceIndex < 0 )
				return false;

			const THandle< MeshAppearance >& appearance = GetAppearanceData( appearanceIndex );
			if ( chunkIndex >= appearance->m_chunkMaterials.Size() )
				return false;

			red::DynArray< CName > newMaterialNames;
			MaterialLayerHelpers::ApplySingleChunk( newMaterialNames, *this, layeredSetup.Front() );
			appearance->m_chunkMaterials[ chunkIndex ] = newMaterialNames.Front();
			appearance->ForceReloadRenderMaterialSetup( false, false );
			RemoveUnusedMaterials();
			return true;
		}

		return false;
#endif
	}

	//------

	ChunkStreamData DataView::GetChunkStreamDataForWriting(ChunkIndex chunkIndex, EMeshStreamType meshStream) const
	{
		if (chunkIndex >= m_mesh->m_chunks.Size())
			return ChunkStreamData();

		auto& chunk = m_mesh->m_chunks[chunkIndex];

		const SMeshStream* stream = nullptr;
		for (auto& currentStream : chunk.m_streams)
		{
			if (currentStream.m_type == meshStream)
			{
				stream = &currentStream;
				break;
			}
		}

		if ( !stream )
			return ChunkStreamData();

		auto buffer = stream->m_data.Editor_LoadContentDirect();
		if ( !buffer.Data() )
			ChunkStreamData();
				
		ChunkStreamData data;
		data.m_stream = meshStream;
		data.m_buffer = serialization::BufferHandleRO( std::move( buffer ) );
		data.m_data = const_cast< void* >( data.m_buffer.Data() );
		data.m_stride = MeshPackerUtils::GetMeshStreamSize(stream->m_type);
		data.m_count = data.m_buffer.GetSize() / data.m_stride;

		return data;
	}

	Bool DataView::AddChunkStream( ChunkIndex chunkIndex, EMeshStreamType meshStream )
	{
		if ( chunkIndex >= m_mesh->m_chunks.Size() )
			return false;

		SMeshChunkPacked& chunk = m_mesh->m_chunks[chunkIndex];

		for ( auto& currentStream : chunk.m_streams )
		{
			if ( currentStream.m_type == meshStream )
			{
				return false;
			}
		}

		Uint32 numVerts = chunk.m_numVertices;
		Uint32 streamElementSize = MeshPackerUtils::GetMeshStreamSize( meshStream );
		Uint32 streamDataSize = streamElementSize * numVerts;

		SMeshStream& newStream = chunk.m_streams.EmplaceBack();
		newStream.m_type = meshStream;
		newStream.m_data.Editor_SetContentDirect( streamDataSize );

		chunk.m_streamMask |= meshStream;
		m_recalculateGeometryHash = true;
		return true;
	}

	bool DataView::RecomputeVertexEpsilons()
	{
		bool result = false;

		if ( m_mesh->m_boneVertexEpsilons.Empty() )
			return false;

		// Clear all epsilons.
		for (Uint16 epsilonIndex = 0; epsilonIndex < m_mesh->m_boneVertexEpsilons.Size(); epsilonIndex++)
		{
			m_mesh->m_boneVertexEpsilons[epsilonIndex] = 0.0f;
		}

		// Recompute from skinning.
		for (Uint32 chunkIndex = 0; chunkIndex < GetNumChunks(); ++chunkIndex)
		{
			StreamIteratorRO<Vector3> positions(GetChunkStreamData(chunkIndex, MST_Position_3F));
			StreamIteratorRO<Vector4> weights(GetChunkStreamData(chunkIndex, MST_SkinningWeights_4F));
			StreamIteratorRO<Color> indices(GetChunkStreamData(chunkIndex, MST_SkinningIndices_4U8));

			StreamIteratorRO<Vector4> extWeights(GetChunkStreamData(chunkIndex, MST_SkinningWeightsExt_4F));
			StreamIteratorRO<Color> extIndices(GetChunkStreamData(chunkIndex, MST_SkinningIndicesExt_4U8));
			if (!positions.IsValid())
			{
				RED_LOG_ERROR("Failed to calculate bounding epsilons for mesh chunk, because there is no valid position stream");
				continue;
			}

			if (!weights.IsValid() || !indices.IsValid())
			{
				continue;
			}
			result = true;
			for (Uint32 vertIndex = 0; vertIndex < GetChunkVertexCount(chunkIndex); vertIndex++)
			{
				Vector3 pstn = positions[vertIndex];
				float wght[8] = { weights[vertIndex].X, weights[vertIndex].Y, weights[vertIndex].Z, weights[vertIndex].W, 0.0f, 0.0f, 0.0f, 0.0f };
				Uint8 indc[8] = { indices[vertIndex].R, indices[vertIndex].G, indices[vertIndex].B, indices[vertIndex].A, 0, 0, 0, 0 };

				if (extIndices.IsValid() && extWeights.IsValid())
				{
					wght[4] = extWeights[vertIndex].X; wght[5] = extWeights[vertIndex].Y; wght[6] = extWeights[vertIndex].Z; wght[7] = extWeights[vertIndex].W;
					indc[4] = extIndices[vertIndex].R; indc[5] = extIndices[vertIndex].G; indc[6] = extIndices[vertIndex].B; indc[7] = extIndices[vertIndex].A;
				}

				for (Uint16 weightIndex = 0; weightIndex < 8; weightIndex++)
				{
					if (wght[weightIndex] == 0)
						continue;

					Vector3 rigmatrixPosition = GetBoneMatrix(indc[weightIndex]).OrthonormInverted().TransformPoint(Vector3());
					float epsilon = GetBoneVertexEpsilon(indc[weightIndex]);

					float dist = (pstn - rigmatrixPosition).Mag();
					if (dist > epsilon)
						m_mesh->m_boneVertexEpsilons[indc[weightIndex]] = dist;
				}
			}
		}
		return result;
	}



    union MeshLODBone
    {
        Uint32 all;
        struct
        {
            Uint16 index;
            Uint8 lodMask;
        };
    };

	bool DataView::CreateSkinningLODs()
	{
		const Uint32 nbBones = GetNumBones();
		if( nbBones == 0 )
			return false;

		const Uint32 nbChunks = GetNumChunks();

		red::StaticArray<Uint32, 9> skinningHistogram;
		red::DynArray<Uint32> boneUsageHistogram;
		
		boneUsageHistogram.Resize( nbBones, 0 );

		red::DynArray<MeshLODBone> boneRemapScratch;
        boneRemapScratch.Reserve( nbBones );
        for( Uint32 i = 0; i < nbBones; ++i )
        {
            MeshLODBone& b = boneRemapScratch.EmplaceBack();
            b.index = i;
            b.lodMask = 0;
        }

		red::DynArray<Uint32> boneChunkBuckets;
		boneChunkBuckets.Reserve( nbChunks );



        const Uint32 nbLODs = GetNumLODs();
        for( Uint32 ilod = 0; ilod < nbLODs; ++ilod )
        {
            mesh::utils::TChunkIndices lodChunkIndices;
            mesh::utils::GetLODChunks( *this, ilod, lodChunkIndices );

            for( Uint32 ichunkIndex = 0; ichunkIndex < lodChunkIndices.Size(); ++ichunkIndex )
            {
                const Uint32 ichunk = lodChunkIndices[ichunkIndex];

                memset( boneUsageHistogram.Data(), 0x00, boneUsageHistogram.DataCapacity() );
                CalcChunkSkinningHistogram( ichunk, skinningHistogram, &boneUsageHistogram );
                                
                for( Uint32 ibone = 0; ibone < nbBones; ++ibone )
                {
                    if( boneUsageHistogram[ibone] )
                    {
                        boneRemapScratch[ibone].lodMask |= 1 << ilod;
                    }
                }
            }
        }

        std::sort( boneRemapScratch.Begin(), boneRemapScratch.End(), []( const MeshLODBone& a, const MeshLODBone& b )
        {
            if( a.lodMask == b.lodMask )
                return a.index < b.index;

            const Uint32 popA = red::BitUtils::PopulationCount<Uint32>( (Uint32)a.lodMask );
            const Uint32 popB = red::BitUtils::PopulationCount<Uint32>( (Uint32)b.lodMask );

            if( popA == popB )
            {
                return a.lodMask > b.lodMask;
            }
            return popA > popB;

        } );

        struct SkinningIndices
        {
            Uint8 abcd[4];
        };
		struct Helper
		{
			static void Remap( red::ArraySpan<SkinningIndices> indices, red::ArraySpan< const MeshLODBone > remap )
			{
				for( Uint32 ii = 0; ii < indices.Size(); ++ii )
				{
                    for( Uint32 j = 0; j < 4; ++j )
                    {
                        const Uint8 vertexBoneIndex = indices[ii].abcd[j];
                        for( Uint32 i = 0; i < remap.Size(); ++i )
                        {
                            if( vertexBoneIndex == remap[i].index )
                            {
                                indices[ii].abcd[j] = i;
                            }
                        }
                    }
				}
			}
		};

        for( Uint32 ilod = 0; ilod < nbLODs; ++ilod )
        {
            mesh::utils::TChunkIndices lodChunkIndices;
            mesh::utils::GetLODChunks( *this, ilod, lodChunkIndices );

            for( Uint32 ichunkIndex = 0; ichunkIndex < lodChunkIndices.Size(); ++ichunkIndex )
            {
                const Uint32 ichunk = lodChunkIndices[ichunkIndex];
             
                red::ArraySpan<SkinningIndices> indices = MakeSpan<SkinningIndices>( GetChunkStreamData( ichunk, MST_SkinningIndices_4U8 ) );
                red::ArraySpan<SkinningIndices> extIndices = MakeSpan<SkinningIndices>( GetChunkStreamData( ichunk, MST_SkinningIndicesExt_4U8 ) );

                Helper::Remap( indices, boneRemapScratch );
                Helper::Remap( extIndices, boneRemapScratch );
            }
        }

        CMesh::TSkeletonBoneNameArray	 oldBoneNames = std::move( m_mesh->m_boneNames );
        CMesh::TSkeletonBoneMatrixArray	 oldBoneRigMatrices = std::move( m_mesh->m_boneRigMatrices );
        CMesh::TSkeletonBoneEpsilonArray oldBoneVertexEpsilons = std::move( m_mesh->m_boneVertexEpsilons );

        m_mesh->m_lodBoneMask.Clear();
        m_mesh->m_lodBoneMask.Reserve( boneRemapScratch.Size() );
        m_mesh->m_boneNames.Reserve( boneRemapScratch.Size() );
        m_mesh->m_boneRigMatrices.Reserve( boneRemapScratch.Size() );
        m_mesh->m_boneVertexEpsilons.Reserve( boneRemapScratch.Size() );

        for( const MeshLODBone& b : boneRemapScratch )
        {
            m_mesh->m_lodBoneMask.PushBack( b.lodMask );
            m_mesh->m_boneNames.PushBack( oldBoneNames[b.index] );
            m_mesh->m_boneRigMatrices.PushBack( oldBoneRigMatrices[b.index] );
            m_mesh->m_boneVertexEpsilons.PushBack( oldBoneVertexEpsilons[b.index] );
        }

		m_recreateBlob = true;
		m_recreateRenderResource = true;

		return true;
	}

	void DataView::ForceRecreateMaterialSetup()
	{
		m_recreateMaterialSetup = true;
	}

	void DataView::ForceRecreateBlob()
	{
		m_recreateBlob = true;
	}

	void DataView::ForceRecreateRenderResource()
	{
		m_recreateRenderResource = true;
	}

	void DataView::ForceRecalculateGeometryHash()
	{
		m_recalculateGeometryHash = true;
	}

	void DataView::ForceSetSaveDate( const CDateTime& newDateTime )								/// @todo DP: this is E3 HACK, remove this!
	{
		m_mesh->m_saveDateTime = newDateTime;
	}

	void DataView::MarkLODAsGenerated()
	{
		m_mesh->m_generatedLOD = true;
	}

	void DataView::RemoveParameter( const rtti::ClassType* type )
	{
		m_mesh->RemoveParameter(type);
	}

} // mesh

//#pragma optimize("",on)
