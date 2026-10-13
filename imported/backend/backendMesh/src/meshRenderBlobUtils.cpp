/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "meshRenderBlobUtils.h"
#include "meshOptimizer.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../backend/backendMesh/include/meshCompilationSource.h"
#include "../../../common/renderBackend/include/renderMeshCompiler.h"

 //**************************************************************************************************************************//
 //**************************************************************************************************************************//

namespace tools
{
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

	class RenderMeshCompilerProcess : public IRenderMeshCompilerProcess
	{
	public:

		virtual void ProcessIndices( red::ArraySpan<Uint16> indices, Uint32 numVertices ) override
		{
			mesh::ApplyVertexCacheOptimization( indices, numVertices );
		}
	};

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

	Bool CreateMeshRenderResourceBlob( CMesh& mesh, ECookingPlatform platform, Bool verbose )
	{
		CMeshCompilationSource source( mesh, platform, verbose );

		tools::RenderMeshBlobCompiler::Settings settings;
		settings.m_platform = platform;

		tools::RenderMeshBlobCompiler compiler( settings );

		RenderMeshCompilerProcess process;

		THandle< IRenderResourceBlob > blob;
		if( !compiler.Compile( source, &process, blob ) )
		{
			return false;
		}

		mesh.SetRenderResourceBlob( blob );

		return true;
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

} // namespace tools

//**************************************************************************************************************************//
//**************************************************************************************************************************//
