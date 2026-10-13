#include "build.h"
#include "removeLodCommand.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../backendMesh/include/meshDataView.h"
#include "../../backendEngine/include/resourceMetadata.h"
#include "../../backendEngine/include/resourceOptions.h"
#include "../../../common/resource/include/resourceSaver.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( RemoveLodCommand, tools );
	RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

namespace tools
{
	class RemoveLodOptions : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( RemoveLodOptions );

	public:
		virtual Bool NeedOptions() const override
		{
			return true;
		}

		Int32 m_triangleCount = -1;

		Bool m_LOD0 = false;
		Bool m_LOD1 = false;
		Bool m_LOD2 = false;
		Bool m_LOD3 = false;
	};

	//////////////////////////////////////////////////////////////////////////////////////

	Bool RemoveLodCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< CMesh >();
	}

	CName RemoveLodCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "removeLOD" );
	}

	ResourceCommandResult RemoveLodCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		return RemoveLods( Cast< RemoveLodOptions >( options ) );
	}

	THandle< ResourceCommandOptions > RemoveLodCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions /*= nullptr */ ) const
	{
		if ( suggestedOptions && suggestedOptions->IsA< RemoveLodOptions >() )
		{
			return suggestedOptions;
		}
		return CreateHandle< RemoveLodOptions >();
	}

	ResourceCommandResult RemoveLodCommand::RemoveLods( const THandle< RemoveLodOptions >& options ) const
	{
		if ( !options )
		{
			return ResourceCommandResult::Error( String::Printf( "Missing options" ) );
		}

		res::LoadingOptions loadingOptions;
		loadingOptions.skipPostLoad = false;
		
		THandle< CMesh > mesh = GResourceLoader->IssueLoadingRequest( Convert( GetResourcePath(), loadingOptions ) )->WaitUntilLoaded< CMesh >();
		if ( !mesh )
		{
			return ResourceCommandResult::Error( String::Printf( "Unable to load" ) );
		}

		red::DynArray< Bool > shouldRemoveLOD{ options->m_LOD0, options->m_LOD1, options->m_LOD2, options->m_LOD3 };
		Bool lod0AlreadyRemoved = false;
		Bool failedToRemove = false;

		// remove specified LODs firstly
		THandle< res::ResourceMetadata > metadata = res::ResourceMetadata::Get( GetResourcePath(), true );
		for ( Int32 lodInd = 3; lodInd >= 0; lodInd-- )
		{
			if ( !shouldRemoveLOD[lodInd] )
			{
				continue;
			}

			if ( lodInd == 0 )
			{
				if ( metadata && metadata->GetTags().Exist( RED_NAME_CONSTEXPR( "lod0Removed" ) ) )
				{
					lod0AlreadyRemoved = true;
					continue;
				}
			}

			mesh::DataView meshDataView( *mesh.Get() );
			if ( lodInd >= meshDataView.GetNumLODs() )
			{
				continue;
			}

			red::DynArray< Float > distances;
			for ( Uint32 i = 0; i < meshDataView.GetNumLODs(); ++i )
			{
				distances.PushBack( meshDataView.GetLODDistance( i ) );
			}

			if ( meshDataView.RemoveLODWithData( lodInd ) )
			{
				for ( Uint32 i = 0; i < meshDataView.GetNumLODs(); ++i )
				{
					meshDataView.SetLODDistance( i, distances[i] );
				}

				if ( lodInd == 0 )
				{
					// set a tag lod0Removed
					if ( !metadata )
					{
						metadata = CreateHandle< res::ResourceMetadata >();
					}
					res::ResourceMetadata::TFileTags& tags = metadata->GetTags();
					tags.PushBack( RED_NAME_CONSTEXPR( "lod0Removed" ) );
					res::ResourceMetadata::Set( mesh, metadata );
				}

				meshDataView.Apply();
			}
			else
			{
				failedToRemove = true;
			}
		}

		// then check the triangles count
		if ( options->m_triangleCount >= 0 )
		{
			mesh::DataView meshDataView( *mesh.Get() );
			red::DynArray< Float > distances;
			for ( Uint32 i = 0; i < meshDataView.GetNumLODs(); ++i )
			{
				distances.PushBack( meshDataView.GetLODDistance( i ) );
			}

			for ( Int32 lodInd = meshDataView.GetNumLODs() - 1; lodInd > 0; lodInd-- ) // > 0, cause in case we got to 0, it means it's the last lod in the mesh anyway
			{
				Int32 lodTrianglesCount = 0;
				const red::DynArray< Uint16 > chunkIndices = meshDataView.GetLODChunkIndices( lodInd );
				for ( const Uint16& chunkInd : chunkIndices )
				{
					lodTrianglesCount += meshDataView.GetChunkTriangleCount( chunkInd );
				}

				if ( lodTrianglesCount >= options->m_triangleCount )
				{
					break;
				}

				if ( meshDataView.RemoveLODWithData( lodInd ) )
				{
					for ( Uint32 i = 0; i < meshDataView.GetNumLODs(); ++i )
					{
						meshDataView.SetLODDistance( i, distances[i] );
					}
					meshDataView.Apply();
				}
				else
				{
					failedToRemove = true;
				}
			}
		}

		if ( !res::SaveResource( mesh ) )
		{
			return ResourceCommandResult::Error( String::Printf( "Unable to remove LODs because saving failed" ) );
		}

		if ( lod0AlreadyRemoved )
		{
			return ResourceCommandResult::Success( mesh, String::Printf( "LOD0 already removed" ) );
		}

		if ( failedToRemove )
		{
			mesh::DataView meshDataView( *mesh.Get() );
			if ( meshDataView.GetNumAppearances() == 1 )
			{
				return ResourceCommandResult::Success( mesh, String::Printf( "Some LODs were not removed: there is only one LOD left" ) );
			}
			else
			{
				return ResourceCommandResult::Error( String::Printf( "Failed to remove some LODs" ) );
			}
		}
		return ResourceCommandResult::Success( mesh );
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( RemoveLodOptions, tools );
	RTTI_PARENT_TYPE( ResourceCommandOptions );

	RTTI_PROPERTY( m_triangleCount ).editable().tooltip( "LODs with lower triangles count will be removed" );
	RTTI_PROPERTY( m_LOD0 ).editable();
	RTTI_PROPERTY( m_LOD1 ).editable();
	RTTI_PROPERTY( m_LOD2 ).editable();
	RTTI_PROPERTY( m_LOD3 ).editable();
RTTI_END_TYPE();