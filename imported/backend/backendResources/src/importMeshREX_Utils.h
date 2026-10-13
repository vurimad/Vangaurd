/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include <vector>
#include "re_archive/reFileArchive.h"


using red::String;

class CMesh;

namespace vis
{
	class IOccluderResource;
}

namespace mesh
{
	class MeshImportedSnapPoint;
	class DataBuilder;
}

void ImportGarmentSupportData( THandle< CMesh >& meshData, std::vector< ReFileMeshPtr >& meshes );
void ImportBBoxData( const ReFileMeshPtr& mesh, THandle< CMesh >& meshData, Bool& rebuildBBox );
void ImportSnapFeatures( std::vector< ReFileMeshPtr >& meshes, THandle< CMesh >& meshData );
void ImportMeshDataParameters( std::vector< ReFileMeshPtr >& meshes, THandle< CMesh >& meshData );
ImportResult ImportOcclusionData( std::vector< ReFileOcclusionMeshPtr >& reOccluders, const red::AbsolutePath sourceFilePath, THandle< CREXMeshResourceImportOptions >& options, THandle< CMesh >& mesh );
Bool ImportFloatTrackNames( ReFileMeshPtr mesh, mesh::DataBuilder& buildData );
void ImportWorkspotOffsets( const std::vector< ReFileMeshPtr >& meshes, THandle< CMesh >& meshData );
