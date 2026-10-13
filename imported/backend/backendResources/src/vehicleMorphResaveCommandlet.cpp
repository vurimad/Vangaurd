#include "build.h"
#include "resaveCommandlet.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshChunk.h"
#include "../../backendMesh/include/meshDataView.h"

// This commandlet removes obsolete vertex streams: MST_VehicleDmgNormalSides_3F, MST_VehicleDmgPosSides_3F

class BACKEND_RESOURCES_API VehicleMorphResaveCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( VehicleMorphResaveCommandlet );

public:
	VehicleMorphResaveCommandlet()
		: ResaveCommandlet( RED_NAME_CONSTEXPR( "vehicleMorphResave" ) )
	{}

protected:
	virtual bool OnResourcePreSave( CResource & resource ) override
	{
		// Skip non-meshes
		const char expectedExt[] = "mesh";
		if ( strcmp( expectedExt, resource.GetExtension() ) )
		{
			return false;
		}

		// Skip proxies
		Uint32 indexFound = resource.GetPath().ToStringView().Find( "proxy" );
		if ( indexFound != red::StringView::npos )
		{
			return false;
		}

		CMesh& mesh = ( CMesh& )resource;
		mesh::DataView dataView( mesh );

		constexpr Uint32 streamsToRemove = MST_VehicleDmgNormalSides_3F | MST_VehicleDmgPosSides_3F;

		Bool needsResave = false;
		for ( mesh::ChunkIndex chunkIdx = 0; chunkIdx < dataView.GetNumChunks(); ++chunkIdx )
		{
			// Check if mesh contains these streams
			if ( ( streamsToRemove & dataView.GetChunkStreamMask( chunkIdx ) ) != 0 )
			{
				needsResave = true;
				break;
			}
		}
		
		if ( !needsResave )
		{
			return false;
		}

		dataView.RemoveMeshStreams( streamsToRemove, true );

		// dataView.ForceRecreateBlob();
		// dataView.ForceRecreateRenderResource();
		dataView.Apply();
		return true;
	}
};

RTTI_BEGIN_TYPE( VehicleMorphResaveCommandlet )
RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()
