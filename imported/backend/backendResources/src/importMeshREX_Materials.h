/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include <vector>
#include "re_archive/reFileArchive.h"
#include "importMeshREX_Common.h"

class CMesh;

namespace mesh
{
	class DataView;
}

void ImportMaterials( THandle< CMesh >& parentMesh, const std::vector< ReFileMeshPtr >& meshes, mesh::DataView& buildData, Bool replaceExistingMaterials = true );
