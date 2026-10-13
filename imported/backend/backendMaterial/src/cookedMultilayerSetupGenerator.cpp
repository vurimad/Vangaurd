/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendData/include/generator.h"
#include "../../../common/resourceMaterial/include/multilayerSetup.h"
#include "../../backendData/include/directoryDependency.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../backendData/include/cooker.h"
#include "../../backendData/include/dataErrorHandler.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendData/include/dataPlatform.h"
#include "../../backendData/include/dependencyUtils.h"

class CookedMultilayer_Setup_Generator : public IGenerator
{
	RTTI_DECLARE_TYPE( CookedMultilayer_Setup_Generator );

public:
	virtual ~CookedMultilayer_Setup_Generator() override = default;

	virtual bool CanGenerate( const res::ResourcePath& resourcePath ) const override;
	virtual bool CollectDependencies( const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system ) const override;
	virtual bool Generate( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const override;
	virtual Uint32 GetVersion() const override;
	virtual data::DepotLocation GetOutputDepot() const override;

private:

};

RTTI_BEGIN_TYPE( CookedMultilayer_Setup_Generator );
RTTI_PARENT_TYPE( IGenerator );
RTTI_END_TYPE();

bool CookedMultilayer_Setup_Generator::CanGenerate( const res::ResourcePath& resourcePath ) const
{
	return resourcePath == CookedMultilayer_Setup::GetCookedResourcePath();
}

bool CookedMultilayer_Setup_Generator::CollectDependencies( const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system ) const
{
	collector.Add( MakeDirectoryDependency( "base\\", "*.mlsetup", true ) );
	return true;
}

bool CookedMultilayer_Setup_Generator::Generate( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const
{
	red::DynArray< res::ResourceTokenHandle > dependencies{ red::PoolBackend() };

	{
		const red::DynArray< res::ResourcePath > resourceDependencies = commandlet::ScanResourceDepotDirectories(system.SourceDepot(), "base\\", "*.mlsetup");
		for (const res::ResourcePath& dependencyPath : resourceDependencies)
		{
			res::IssueLoadingRequestParameter param;
			param.path = dependencyPath;
			param.postLoadFlags.skipUploadRenderData = true;
			auto token = system.SourceLoader().IssueLoadingRequest(param);
			dependencies.PushBack(token);
			builder.DispatchWait(token->GetWaitCounter());
		}
	}

	builder.DispatchJob( "CookMultilayerSetup", [path, &system, dependencies]( const job::RunContext& context )
	{
		THandle< CookedMultilayer_Setup > resource = CreateHandle< CookedMultilayer_Setup >();

		red::DynArray< THandle< CResource > > inplaceResources{red::PoolBackend()};

		inplaceResources.Reserve( dependencies.Size() );
		for( const res::ResourceTokenHandle& token : dependencies )
		{
			if( !token->HasFailed() )
			{
				inplaceResources.PushBack( token->GetResource() );
				resource->AddDependency( token->GetPath() );
			}
		}

		if ( !dependency::InplaceTopologicalSort( system.SourceResourceDatabase(), inplaceResources ) )
		{
			system.ErrorHandler().ReportError( "Multilayer Setup cooker: Failed to sort inplace resources for '%hs'", path.ToDebugString() );
			system.ErrorHandler().Fail();
			return;
		}

		bool saveResult = cooker::SaveResource( path, resource, system.CookedDepot(), system.TargetPlatform(), inplaceResources );
		if( !saveResult )
		{
			system.ErrorHandler().ReportError( "Multilayer Setup cooker: Failed to save '%hs'", path.ToDebugString() );
			system.ErrorHandler().Fail();
			return;
		}
	} );


	return true;
}

Uint32 CookedMultilayer_Setup_Generator::GetVersion() const
{
	return 4;
}

data::DepotLocation CookedMultilayer_Setup_Generator::GetOutputDepot() const
{
	return data::DepotLocation::Cooked;
}
