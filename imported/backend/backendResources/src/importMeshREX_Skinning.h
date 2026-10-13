/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include <vector>
#include "re_archive/reFileArchive.h"

namespace mesh
{
	class DataBuilder;
}

void ImportRigMatrices( const std::vector< ReFileMeshPtr >& meshes, mesh::DataBuilder& buildData, red::DynArray< red::DynArray< Int32 > >& rigMapping );
