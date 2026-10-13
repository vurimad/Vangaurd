/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendEngine/include/validator.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendData/include/dependencyScanner.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../../common/redJobs2/include/jobBuilder.h"

// Validates that the resource's dependencies (imports) are located in appropriate
// directories within the depot.
class ResourceImportValidator : public IValidator
{
	RTTI_DECLARE_TYPE( ResourceImportValidator );
public:
	ResourceImportValidator();
	~ResourceImportValidator();

	virtual bool Validate( const THandle< CResource >& resource, tools::IMessageReporter& errorHandler ) const override;
};

RTTI_BEGIN_TYPE( ResourceImportValidator )
	RTTI_PARENT_TYPE( IValidator )
RTTI_END_TYPE()


ResourceImportValidator::ResourceImportValidator()
	: IValidator( ClassID< CResource >() )
{
}

ResourceImportValidator::~ResourceImportValidator()
{
}

bool ResourceImportValidator::Validate( const THandle< CResource >& resource, tools::IMessageReporter& errorHandler ) const
{
	// NOTE: This is not the best solution but we basically need an editor depot specifically
	// because it is the only one with the knowledge of the resource level stack that we are checking
	// in this validator
	auto editorDepot = commandlet::GetEditorDepot();
	RED_FATAL_ASSERT( editorDepot != nullptr, "Cannot do this operation without an Editor Depot!" );

	// Use the dependency scanner here to open the resource again and grab the first level of dependencies
	// This is done because it is more reliable than the mappers and can handle entity templates correctly
	const res::ResourcePath &resourcePath = resource->GetPath();
	if ( !resourcePath.IsValid() )
	{
		return false;
	}

	red::DynArray< res::ResourcePath > dependencies;
	if ( !dependency::ScanResource( editorDepot, resourcePath, dependencies ) )
	{
		errorHandler.Report( tools::MessageSeverity::Error, resourcePath, "Could not get dependencies from resource" );
		return false;
	}

	Uint32 resourceLevel = editorDepot->GetResourceLevel( resourcePath );

	for ( const auto& depPath : dependencies )
	{
		Uint32 depLevel = editorDepot->GetResourceLevel( depPath );
		if ( depLevel > resourceLevel )
		{
			errorHandler.Report( tools::MessageSeverity::Error, resourcePath, RED_LOCATION( depPath ),
				"Imported resource % violates depot constraints (level % > %)", depPath, depLevel, resourceLevel );
		}

		res::ResourcePath resolvedPath = editorDepot->ResolveResourcePath( depPath );
		if ( !resolvedPath.IsValid() )
		{
			errorHandler.Report( tools::MessageSeverity::Warning, resourcePath, RED_LOCATION( depPath ), "Resource import is invalid or missing" );
		}
		else if ( resolvedPath != depPath )
		{
			Uint32 resolvedLevel = editorDepot->GetResourceLevel( resolvedPath );
			if ( resolvedLevel > resourceLevel )
			{
				errorHandler.Report( tools::MessageSeverity::Error, resourcePath, RED_LOCATION( resolvedPath ),
					"Imported resource % through link % violates depot constraints (level % > %)", resolvedPath, depPath, resolvedLevel, resourceLevel );
			}
		}
	}

	return true;
}


class ResourceReferencesValidator : public IValidator
{
	RTTI_DECLARE_TYPE( ResourceReferencesValidator );
public:
	ResourceReferencesValidator();
	~ResourceReferencesValidator();

	virtual bool Validate( const THandle< CResource >& resource, tools::IMessageReporter& errorHandler ) const override;

private:
	static void ProcessReference( const res::ResourcePath& path, job::Counter& jobCounter, red::Set< res::ResourcePath >& checked, red::RWSpinLock& lock, tools::IMessageReporter& errorHandler, const resdb::Database& database );
};

RTTI_BEGIN_TYPE( ResourceReferencesValidator )
RTTI_PARENT_TYPE( IValidator )
RTTI_END_TYPE()


ResourceReferencesValidator::ResourceReferencesValidator()
	: IValidator( ClassID< CResource >() )
{
}

ResourceReferencesValidator::~ResourceReferencesValidator()
{
}

Bool ResourceReferencesValidator::Validate( const THandle< CResource >& resource, tools::IMessageReporter& errorHandler ) const
{
	return true;
	// this requires resdb initialisation for each resource on the changelist to submit
	// and hence should not be done until we revisit the case of one common commandlet process

	commandlet::BackendDepotHolder depotHolder;
	red::AbsolutePath resdbCachePath = depotHolder.GetCachePath().AddFilePath( resdb::Database::GetDefaultFilename() );

	resdb::Database database;
	database.Initialise( *depotHolder.GetDepot(), resdbCachePath );

	resdb::Links references = database.GetForwardLinks( resource->GetPath() );
	Bool result = true;

	red::Set< res::ResourcePath > checked;
	checked.Insert( resource->GetPath() );

	for ( const resdb::Link& reference : references )
	{
		if ( !reference.to.ToString().BeginsWith( "base" ) && !reference.to.ToString().BeginsWith( "engine" ) )
		{
			result = false;
			{
				errorHandler.Report( tools::MessageSeverity::Error, "Non-production asset reference '%' found in '%'",
					reference.to.ToDebugString(), resource->GetPath().ToDebugString() );
			}
		}
	}
	return result;
}

void ResourceReferencesValidator::ProcessReference( const res::ResourcePath& path, job::Counter& jobCounter, red::Set<res::ResourcePath>& checked, red::RWSpinLock& lock, tools::IMessageReporter& errorHandler, const resdb::Database& database )
{
	job::Builder jobBuilder;
	resdb::Links references = database.GetForwardLinks( path );
	for ( const resdb::Link& reference : references )
	{
		if ( !reference.to.ToString().BeginsWith( "base" ) && !reference.to.ToString().BeginsWith( "engine" ) )
		{
			RED_SCOPE_LOCK( lock );
			errorHandler.Report( tools::MessageSeverity::Warning, "Non-production asset reference '%' found in '%'",
				reference.to.ToDebugString(), path.ToDebugString() );
		}
		else
		{
			{
				RED_SCOPE_LOCK( lock );
				if ( checked.Exist( reference.to ) )
				{
					continue;
				}
				checked.Insert( reference.to );
			}

			jobBuilder.DispatchJob( "ReferenceValidator", [&jobCounter, reference, &checked, &lock, &errorHandler, &database] ( const job::RunContext& ctx )
			{
				ProcessReference( reference.to, jobCounter, checked, lock, errorHandler, database );
			} );
		}
	}

	jobCounter += jobBuilder.ExtractWaitCounter();
}
