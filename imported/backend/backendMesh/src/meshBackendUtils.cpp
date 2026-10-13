/**
* Copyright (c) 2008 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshBackendUtils.h"
#include "../include/meshDataView.h"
#include "../include/meshDataBuilder.h"
#include "../../../common/worldStatic/include/bendedMeshNode.h"
//#include "../../../common/world/include/meshNode.h"

namespace mesh
{
	red::DynArray<String> GetAllBodyTypeNames()
	{
											/// @todo DP: share this with CharacterResource code, maybe load from "base\\characters\\bodyTypesMap.csv"
		static const red::StaticArray<String, 10 > c_bodyTypesAbbr = { "ma", "mb", "mf", "mc", "wa", "wf", "wc", "mba", "wba", "waf" };
		return red::DynArray<String>( c_bodyTypesAbbr );
	}

	String ExtractBodyTypeNameFromMeshPath( red::StringView meshPath )						/// @todo DP: also share this code with mesh editor (backend)
	{
		red::StringView meshName = red::paths::GetFileStem( meshPath );

		for ( const String& bodyTypeAbbr : GetAllBodyTypeNames() )
		{
			String bodyTypeAbbrWithSeparators = "_" + bodyTypeAbbr + "_";
			if ( meshName.Find( bodyTypeAbbrWithSeparators ) != red::StringView::npos )
				return bodyTypeAbbr;
		}

		return String::EMPTY();
	}

	bool GetDeformedCMeshFromBendedMeshNode( THandle<CMesh> bendedMesh, world::BendedMeshNode &node, CMesh& outMesh, Bool copyMaterials /*= false */)
	{
		mesh::DataBuilder buildData( *bendedMesh );
		red::DynArray< red::DynArray< Vector3 >> bendedVerts = red::PoolBackend();
		auto numChunks = buildData.m_chunks.Size();

		if (!node.Offline_ComputeSkinnedVertices( bendedVerts, bendedMesh ) || numChunks != bendedVerts.Size())
		{
			return false;
		}

		for (Uint32 chunkIdx = 0; chunkIdx < numChunks; chunkIdx++)
		{
			for (auto& stream : buildData.m_chunks[chunkIdx].m_streams)
			{
				if (stream.m_type == MST_Position_3F)
				{
					auto streamData = stream.m_data.GetSpan().Pointer<red::Float>();
					for (Uint32 i = 0; i < bendedVerts[chunkIdx].Size(); i++)
					{
						auto& pos = bendedVerts[chunkIdx][i];
						streamData[3 * i] = pos.X;
						streamData[3 * i + 1] = pos.Y;
						streamData[3 * i + 2] = pos.Z;
					}
					continue;
				}
			}
		}

		mesh::DataBuilder::FlushOptions options;
		if (copyMaterials)
		{
			options.m_uploadResource = true;
			mesh::DataView viewIn( *bendedMesh );
			mesh::DataView viewOut( outMesh );
			Uint32 materialNum = viewIn.GetNumMaterials();
			for (Uint32 i = 0; i < materialNum; ++i)
			{
				viewOut.AddMaterial( viewIn.GetMaterialData( i ), viewIn.GetMaterialName( i ) );
			}
		}
		else
		{
			options.m_uploadResource = false;
		}
		buildData.Flush( outMesh, options );

		//Remove rigmatrixes from deformed mesh
		mesh::DataView viewOut( outMesh );
		viewOut.ResetBones( {}, {}, {} );
		viewOut.Apply();

		return true;
	}
} // mesh
