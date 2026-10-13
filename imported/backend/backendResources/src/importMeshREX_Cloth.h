/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include <vector>
#include "re_archive/reFileArchive.h"

class CMesh;

void ImportGraphicalClothData( THandle< CMesh > meshData, std::vector< ReFileMeshPtr >& meshes );
void ImportPhysicalClothData( THandle< CMesh > meshData, std::vector< ReFileMeshPtr >& meshes );
