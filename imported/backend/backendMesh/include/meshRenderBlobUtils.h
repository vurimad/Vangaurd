/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

class CMesh;
enum ECookingPlatform : Uint8;

namespace tools
{
	BACKEND_MESH_API Bool CreateMeshRenderResourceBlob( CMesh& mesh, ECookingPlatform platform, Bool verbose );
}
