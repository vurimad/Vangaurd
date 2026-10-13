/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/redReflection/include/dataBuffer.h"

namespace mesh
{
	struct UnpackedStream
	{
		DataBuffer		m_data;
		EMeshStreamType m_type;
	};

	/// Mesh data helper - allows you to access and modify the content of the mesh easily
	/// This class is VERY SLOW to use, strict backend only
	class BACKEND_MESH_API DataBuilder
	{
	public:
		DataBuilder(); // empty mesh
		DataBuilder(const DataViewRO& sourceData); // seed the builder from existing data
		~DataBuilder();

		DataBuilder( const DataBuilder& ) = delete;
		DataBuilder& operator=( const DataBuilder& ) = delete;

		struct UnpackedChunk
		{
			EMeshVertexType						m_vertexType;
			red::DynArray< CName >				m_materials{ red::PoolBackend() };
			red::DynArray< UnpackedStream >		m_streams{ red::PoolBackend() };
			DataBuffer							m_topologyData; // flat array of Uint16
			DataBuffer							m_topologyMetadata; // flat array of Uint32 (2 Uint32's per vertex)
			Uint8								m_lodMask;
			ChunkFlags							m_flags;
			String								m_materialUUID;
			String								m_meshUUID;
		};

		struct UnpackedLOD
		{
			Float m_distance;
		};

		struct UnpackedBone
		{
			CName m_name;
			Matrix m_rig;
		};

		typedef red::DynArray< UnpackedChunk > TChunks;
		typedef red::DynArray< UnpackedLOD > TLods;
		typedef red::DynArray< UnpackedBone > TBones;
		typedef red::DynArray< CName > TFloatTracks;

		TChunks m_chunks{ red::PoolBackend() };
		TLods m_lods{ red::PoolBackend() };
		TBones m_bones{ red::PoolBackend() };
		TFloatTracks m_floatTrackNames{ red::PoolBackend() };

		struct FlushOptions
		{
			FlushOptions()
				: m_verbose( false )
				, m_rebuildMaterials( true )
				, m_rebuildLODDistances( false )
				, m_rebuildBBox( true )
				, m_extrudeBBox( false )
				, m_boundingBoxExtrusionFactor(0.f)
				, m_uploadResource( true )
				, m_updateStreaming( true )
				, m_matchByOrder( false )
			{}

			static FlushOptions Default()
			{
				static FlushOptions flushOptions;
				return flushOptions;
			}

			Bool m_verbose;
			Bool m_rebuildMaterials; // Clear all materials, assigning only the shaders (as in the Import Materials flag in .RE importer)
			Bool m_rebuildLODDistances;
			Bool m_rebuildBBox;
			Bool m_extrudeBBox;
			Float m_boundingBoxExtrusionFactor;
			Bool m_uploadResource; // Update the rendering resource (e.g. when you want to refresh the displayed mesh)
			Bool m_updateStreaming;
			Bool m_matchByOrder; // Force the original chunk order. If false, uses UnpackedChunk's m_materialUUID to map them (allows to change the number of chunks or add LODs)
		};

		// flush data to given mesh
		void Flush( CMesh& outMesh, const FlushOptions& flushOptions = FlushOptions::Default() );

		// We can use this for correcting number of chunks in existing appearance, when something got broken in the mesh after reimport.
		static void CorrectNumberOfChunksInAppearance( const CMesh& mesh, const THandle<MeshAppearance>& appearance );

	private:
		// Find mapping for each new chunk, if there is an old chunk that should be the same.
		// Returns the number of unmapped chunks.
		static Uint32 MapNewChunksToOldChunks( red::DynArray<Int32>& chunksMapping, const CMesh& oldMesh, const red::DynArray< UnpackedChunk >& newChunks );

		// Map chunks that are not mapped yet. This can map chunks for new LOD to other chunks in the base LOD.
		static void MapNewChunksToOtherNewChunks( red::DynArray<Int32>& chunksMapping, const red::DynArray< UnpackedChunk >& newChunks );

		// Make sure all new chunks are mapped somewhere.
		static void MapNewChunksFallback( red::DynArray<Int32>& chunksMapping, const red::DynArray< UnpackedChunk >& newChunks, red::Map<Int32, CName>& newMaterialsNames );

		static void CorrectNumberOfChunksInAppearance( const CMesh& mesh, const THandle<MeshAppearance>& appearance, const red::DynArray<Int32>& chunksMapping, red::Map<Int32, CName>& newMaterialsNames);

		// Returns -1 when not found.
		static Int32 FindOldChunkByParams( const CMesh& oldMesh, const red::DynArray< Bool >& oldChunkMappedFlags, Uint8 newLODMask, Uint32 numNewIndices, Uint32 numNewVerts );
		static Int32 FindOldChunkByUUID( const CMesh& oldMesh, const red::DynArray< Bool >& oldChunkMappedFlags, const String& meshUUID, const String& materialUUID, const Uint8& lodMask );
	};

	template<typename T>
	class StreamWriter
	{
	public:
		StreamWriter(DataBuffer& buffer, Uint32 count)
			: m_buffer(buffer)
			, m_count(count)
		{
			m_buffer = DataBuffer(count * sizeof(T));
			m_current = static_cast< T * >( m_buffer.Data() );
		}

		void Write(const T& value)
		{
			RED_FATAL_ASSERT(m_current < static_cast< T * >( m_buffer.Data() ) + m_count, "Out-of-bound write");

			*m_current = value;
			++m_current;
		}

		Uint32 GetNumWrittenElements() const
		{
			return static_cast<Uint32>( m_current - static_cast<const T*>( m_buffer.Data() ) );
		}

	private:
		DataBuffer& m_buffer;
		Uint32 m_count;
		T* m_current;
	};
} // mesh
