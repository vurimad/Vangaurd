#include "build.h"
#include "resaveCommandlet.h"

#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshChunk.h"
#include "../../backendMesh/include/meshDataView.h"

// This commandlet bumps console mesh bias to 1 on selected meshes

class BACKEND_RESOURCES_API MeshConsoleBiasResaveCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( MeshConsoleBiasResaveCommandlet );

public:
	MeshConsoleBiasResaveCommandlet()
		: ResaveCommandlet( RED_NAME_CONSTEXPR( "meshConsoleBiasResave" ) )
	{
	}

protected:

	virtual bool ParseOptions( const red::CommandLine& options ) override
	{
		bool result = false;
		ResaveCommandlet::ParseOptions( options );
		String str{ red::PoolBackend() };
		if ( options.GetFirstOption( "fileListFilePath", str ) )
		{
			String fileListString { red::PoolBackend() };
			red::AbsolutePath pathToFileWithPaths = red::AbsolutePath::ParseFilePath( str );
			if ( red::LoadFileToString( pathToFileWithPaths, fileListString ) )
			{
				red::DynArray<String> filePathList = fileListString.Split( "\r\n" );

				for ( String& filePath : filePathList )
				{
					m_resources.PushBack( res::ResourcePath::Build( filePath ) );
				}
				result = true;
			}
		}
		return result;
	}

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

		mesh.SetConsoleMeshBias( 1 );
		mesh::DataView dataView( mesh );

		return true;
	}
};

RTTI_BEGIN_TYPE( MeshConsoleBiasResaveCommandlet )
RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()
