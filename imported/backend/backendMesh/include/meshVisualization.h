#pragma once

#include "backendMeshApi.h"

#include "../../../common/renderData/include/renderObjectPtr.h"
#include "../../../common/renderData/include/renderResource.h"

class CMesh;
enum EMeshStreamType : Uint32;
class IRenderResource;

namespace tools
{
	BACKEND_MESH_API TRenderPtr<IRenderResource> CreateMeshSkinHeatMap( const THandle< CMesh >& meshData, Int32 boneIndex, Int32 chunkIndexFilter = -1 );

	BACKEND_MESH_API TRenderPtr<IRenderResource> CreateMeshFiltered( THandle< CMesh > meshData, Int32 chunkIndexFilter, CName materialIndexFilter );

	BACKEND_MESH_API TRenderPtr<IRenderResource> CreateMeshStreamPositions( THandle< CMesh > meshData, Int32 chunkIndexFilter );
	BACKEND_MESH_API TRenderPtr<IRenderResource> CreateMeshStreamColor( THandle< CMesh > meshData, Int32 chunkIndexFilter );
	BACKEND_MESH_API TRenderPtr<IRenderResource> CreateMeshStreamGarmentFlags( THandle< CMesh > meshData, Int32 chunkIndexFilter,
	                                                                           red::ArraySpan< const Uint8 > morphOffsetScales = red::ArraySpan< const Uint8 >(),
	                                                                           Bool visualizeCaps = false );
	BACKEND_MESH_API TRenderPtr<IRenderResource> CreateMeshStreamGarmentCaps( THandle< CMesh > meshData, Int32 chunkIndexFilter,
	                                                                          red::ArraySpan< const Uint8 > morphOffsetScales = red::ArraySpan< const Uint8 >() );
	BACKEND_MESH_API TRenderPtr<IRenderResource> CreateMeshStreamIndices( THandle< CMesh > meshData, Int32 chunkIndexFilter );
	BACKEND_MESH_API TRenderPtr<IRenderResource> CreateMeshStreamTBN( THandle< CMesh > meshData, Int32 chunkIndexFilter, EMeshStreamType vectorStream,
	                                                                  const Color& color, Float vectorLength = 0.0f );
	BACKEND_MESH_API TRenderPtr<IRenderResource> CreateMeshStreamMorphOffsetScales( THandle< CMesh > meshData, Int32 chunkIndexFilter,
	                                                                                red::ArraySpan< const Uint8 > morphOffsetScales );

} // tools
