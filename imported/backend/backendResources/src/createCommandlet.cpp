/**
* Copyright c 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "../../backendEngine/include/factory.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/resourceClassLookup.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/resource/include/resourceSaver.h"

/// Commandlet that creates a resource based on the path extension
class CCreateCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( CCreateCommandlet );

public:
	CCreateCommandlet();
	~CCreateCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override { return "Create asset in a depot"; }
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;
};

RTTI_BEGIN_TYPE( CCreateCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()



CCreateCommandlet::CCreateCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "create" ) )
{
}

CCreateCommandlet::~CCreateCommandlet()
{
}

bool CCreateCommandlet::Execute( const red::CommandLine& options )
{	
	res::ResourcePath path;
	String outPath;
	if ( options.GetFirstOption( "path", outPath ) )
		path = res::ResourcePath::Build( outPath.AsChar() );
	else
	{
		RED_LOG_ERROR( "Core: Unable to create '%hs' because parameter 'path' is missing", path.ToString().AsChar() );
		return false;
	}

	// lookup resource class for given extension
	const rtti::ClassType* resourceClass = res::prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( path );
	if ( !resourceClass )
	{
		RED_LOG_ERROR( "Core: Unable to create '%hs' because it's using unknown extension", path.ToString().AsChar() );
		return false;
	}

	// CHECK: request file - it will be loaded if it exists
	res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest( path );
	if ( token->WaitUntilLoaded() )
	{
		RED_LOG_ERROR( "Core: Unable to create '%hs' because it already exists", path.ToString().AsChar() );
		return false;
	}

	// find resource factory for given extension (we are not allowed to create resources without a factory)
	IFactory* factory = IFactory::FindFactory( resourceClass );
	if ( nullptr == factory )
	{
		RED_LOG_ERROR( "Core: Unable to create '%hs' because there's no factory for resource '%hs'", path.ToString().AsChar(), resourceClass->GetName().AsChar() );
		return false;
	}

	// create an empty resource
	THandle< CResource > res = resourceClass->CreateHandle< CResource >();
	if ( !res )
	{
		RED_LOG_ERROR( "Core: Unable to create '%hs' because resource '%hs' could not be created", path.ToString().AsChar(), resourceClass->GetName().AsChar() );
		return false;
	}

	// use the resource factory to create a new resource
	THandle< FactoryOptions > factoryOptions = factory->CreateOptions( path );
	if ( !factory->DoCreate( res, factoryOptions ) )
	{
		RED_LOG_ERROR( "Core: Unable to create '%hs' because factory for resource '%hs' failed", path.ToString().AsChar(), resourceClass->GetName().AsChar() );
		return false;
	}

	res::ResourceSavingOptions savingOptions;
	savingOptions.m_targetPath = path;
	return res::SaveResource( res, savingOptions );
}

void CCreateCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:"  );
	RED_LOG( "    create -path=<path>" );
}

