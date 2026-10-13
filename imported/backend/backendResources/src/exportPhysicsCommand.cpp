/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "exportPhysicsCommand.h"
#include "../../backendEngine/include/exporter.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/physics/include/physicsSystemResource.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( ExportPhysicsCommand, tools );
RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();



namespace tools
{
	class ExportPhysicsCommandOptions : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( ExportPhysicsCommandOptions );

	public:
		virtual Bool NeedOptions() const override
		{
			return false;
		}
	};

	//////////////////////////////////////////////////////////////////////////

	Bool ExportPhysicsCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< CMesh >();
	}

	CName ExportPhysicsCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "exportPhysics" );
	}

	ResourceCommandResult ExportPhysicsCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		res::LoadingOptions loadingOptions;
		loadingOptions.skipPostLoad = false;

		THandle< CMesh > mesh = GResourceLoader->IssueLoadingRequest( Convert( GetResourcePath(), loadingOptions ) )->WaitUntilLoaded< CMesh >();
		if ( !mesh )
		{
			return ResourceCommandResult::Error( String::Printf( "Unable to load %s", GetResourcePath().ToDebugString() ) );
		}

		IExporter* physExporter = IExporter::FindExporter( ClassID< CMesh >(), String( "phys" ) );

		if ( !physExporter )
		{
			return ResourceCommandResult::Error( String::Printf( "Failed to find IExporter." ) );
		}

		IExporter::ExportOptions exportOptions;
		exportOptions.m_resource = mesh;
		exportOptions.m_saveFileFormat = CFileFormat( "phys", "Extracted mesh' physics data." );
		
		String path = mesh->GetPath().ToString();

		Uint32 extensionIdx = 0u;
		const Bool success = path.IndexOfLast( ".", extensionIdx );
		RED_FATAL_ASSERT( success, "Failed to extract extension from mesh' resource path" );
		
		path = path.LeftString( extensionIdx );
		path.Append( String( ".phys" ) );

		const res::ResourcePath resPath = res::ResourcePath::Build( path );

		if ( !resPath.IsValid() )
		{
			return ResourceCommandResult::Error( String::Printf( "Failed to create a resourcePath for output resource" ) );
		}

		exportOptions.m_saveFilePath = path;

		if ( physExporter->DoExport( exportOptions ) )
		{
			const auto outputRes = GResourceLoader->IssueLoadingRequest( resPath )->WaitUntilLoaded< physics::SystemResource >();

			if ( outputRes )
				return ResourceCommandResult::Success( outputRes );
		}

		return ResourceCommandResult::Error( String( "Unknown error" ) );
	}

	THandle< ResourceCommandOptions > ExportPhysicsCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions /*= nullptr */ ) const
	{
		return CreateHandle< ExportPhysicsCommandOptions >();
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( ExportPhysicsCommandOptions, tools );
RTTI_PARENT_TYPE( ResourceCommandOptions );
RTTI_END_TYPE();