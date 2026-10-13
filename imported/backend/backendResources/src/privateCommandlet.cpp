/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"

#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendWorld/include/minimapDataGeneratorCommon.h"
#include "../../backendEngine/include/exporter.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/gameVehicles/include/vehicleChassisComponent.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/resourceMesh/include/mesh.h"

namespace res
{
	class IBackendResourceDepot;
	class ResourceEditorDepot;
}

namespace commandlet
{
	class FileLinkMap;
	class FileLinkResolver;
}

class BACKEND_RESOURCES_API PrivateCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( PrivateCommandlet );

public:
	PrivateCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override { return "Does whatever you make it do."; }
	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	red::DynArray< res::ResourcePath > m_resources;
	commandlet::EngineInstance m_engineInstance;

	Bool ParseOptions( const red::CommandLine& options );
	void GeneratePhysicsOfVehicle( const ent::EntityTemplate& baseObject );
};

PrivateCommandlet::PrivateCommandlet()
	: red::ICommandlet( RED_NAME( "private" ) )
{}

Bool PrivateCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseOptions( options ) )
		return false;

	job::RunContext ctx;
	job::Builder builder( ctx );
	builder.DispatchParallelForJob( "MainLoop", [this] () { return m_resources.Size(); },
		[this] ( Uint32 index, const job::RunContext& context )
	{
		GResourceLoader->IssueLoadingRequest( m_resources[index] )->OnLoaded( [this] ( THandle< CResource > resource )
		{
			if ( auto entityTemplate = Cast< ent::EntityTemplate >( resource ) )
			{
				GeneratePhysicsOfVehicle( *entityTemplate );
			}
		} );
	} );

	return true;
}

void PrivateCommandlet::PrintHelp() const
{
	printf( "-file=<depot_path> (<depot_path>)" );
}

Bool PrivateCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void PrivateCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

Bool PrivateCommandlet::ParseOptions( const red::CommandLine& options )
{
	m_resources = commandlet::ParseResourcePathsFromCommandLine( *commandlet::GetEditorDepot(), options );

	return true;
}

void PrivateCommandlet::GeneratePhysicsOfVehicle( const ent::EntityTemplate& baseObject )
{
	IExporter* exporter = IExporter::FindExporter( ClassID< CMesh >(), String( "phys" ) );

	job::RunContext ctx;
	job::Builder builder( ctx );
	ent::TemplateUnpackedViewPtr unpackedViewPtr = baseObject.CreateUnpackedView( builder );
	job::FlushCounter( builder.ExtractWaitCounter() );

	for ( ent::ComponentHandle componentHandle : unpackedViewPtr->GetComponents() )
	{
		if ( auto chassisComponent = Cast< vehicle::ChassisComponent >( componentHandle ) )
		{
			IExporter::ExportOptions options;
			options.m_resource = chassisComponent->m_sourceMesh.Get();
			options.m_saveFileFormat = CFileFormat( String( "phys" ), String() );
			res::ResourcePath resPath = chassisComponent->m_sourceMesh.GetPath();
			auto view = resPath.ToStringView();
			view = view.Slice( 0u, view.FindReverse( '.' ) );

			options.m_saveFilePath = view.ToString() + String( ".phys" );

			const Bool success = exporter->DoExport( options );

			if ( !success )
			{
				printf( "Failed to process %s\n", resPath.ToDebugString() );
			}

			chassisComponent->m_collisionResource.SetPath( res::ResourcePath::Build( options.m_saveFilePath ) );
			break;
		}
	}

	unpackedViewPtr->SubmitChanges();
}

RTTI_BEGIN_TYPE( PrivateCommandlet );
RTTI_PARENT_TYPE( red::ICommandlet );
RTTI_END_TYPE();