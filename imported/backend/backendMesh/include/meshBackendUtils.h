/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

# include "meshDataBuilder.h"

namespace world
{
	class BendedMeshNode;
}
class CMesh;

namespace mesh
{
	// Return array of body type abbreviations like "ma" (Man Averga), "wa", "mb" etc.
	BACKEND_MESH_API red::DynArray<String> GetAllBodyTypeNames();

	// Extract body type abbreviation (i.e. "ma", "wa") fro a filename (i.e. "t1_020_wa_tshirt__long_blouse_morph.mesh").
	// Returns empty string when the body type was not found.
	BACKEND_MESH_API String ExtractBodyTypeNameFromMeshPath( red::StringView meshPath );

	BACKEND_MESH_API bool GetDeformedCMeshFromBendedMeshNode( THandle<CMesh> bendedMesh, world::BendedMeshNode &node, CMesh& outMesh, Bool copyMaterials = false );

	template< typename T >
	void WriteToStream( red::DynArray< mesh::UnpackedStream >& chunkStreams, const EMeshStreamType streamType, const Uint32 numUniqueIndexes, const red::DynArray< T >& values, Bool addMissingStream )
	{
		WriteToStream<T, T>( chunkStreams, streamType, numUniqueIndexes, values, addMissingStream );
	}

	template< typename T1, typename T2 >
	void WriteToStream( red::DynArray< mesh::UnpackedStream >& chunkStreams, const EMeshStreamType streamType, const Uint32 numUniqueIndexes, const red::DynArray< T1 >& values, Bool addMissingStream )
	{
		mesh::UnpackedStream* stream = NULL;
		for (auto& currentStream : chunkStreams)
		{
			if (currentStream.m_type == streamType)
			{
				stream = &currentStream;
				break;
			}
		}
		if (!stream && addMissingStream)
		{
			stream = &chunkStreams.EmplaceBack();
			stream->m_type = streamType;
		}

		RED_ASSERT( stream, "Attempting to write to a stream that doesn't exist in current chunk" );
		mesh::StreamWriter< T2 > writer( stream->m_data, numUniqueIndexes );
		for (const auto& val : values)
		{
			writer.Write( val );
		}
	}

} // mesh
