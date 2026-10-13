/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataTypes.h"
#include "dataContext.h"
#include "dependencyCollector.h"
#include "dataResult.h"

namespace dependency
{
class Collector;
}

class BACKEND_DATA_API ICooker
{
	RTTI_DECLARE_POLYMORPHIC_TYPE( ICooker );

public:
	virtual ~ICooker() = 0;

	// Query if this cooker can cook the specified resource
	virtual bool CanCook( const res::ResourcePath& resourcePath ) const = 0;

	// Collect the dependencies for the given resource in the collector object
	// This should be a fast and light weight operation and do as little work as possible
	// It should avoid loading the actual resource or doing heavy processing
	// Return true on success or false if any error occurred and generation should be halted
	// NOTE: This will be run as part of a job
	// NOTE: The default implementation of this just adds the single file as a required dependency
	virtual bool CollectDependencies( const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system ) const = 0;

	// Create jobs to cook the given resource using the provided builder
	// When this is called all of the output and intermediate dependencies have been generated
	// and are up to date.
	// The system context is guaranteed to exist and be valid for the entire execution of the jobs
	// NOTE: This will be called from a job
	virtual bool Cook( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const = 0;

	// Returns the version of the cooker object
	virtual Uint32 GetVersion() const = 0;

	// Returns the name of the cooker
	CName GetName() const;

	// Finds the appropriate cooker class that can process the given resource
	// and then returns a new instance of it.
	static red::UniquePtr< ICooker > Create( const res::ResourcePath& path );
};

namespace cooker
{

BACKEND_DATA_API THandle< CResource > LoadResource( const res::ResourcePath path, data::ResourceDepot& depot );

BACKEND_DATA_API bool SaveResource( const res::ResourcePath path, const THandle< CResource >& resource, data::ResourceDepot& depot, data::TargetPlatform platform );
BACKEND_DATA_API bool SaveResource( const res::ResourcePath path, const THandle< CResource >& resource, data::ResourceDepot& depot, data::TargetPlatform platform, const red::DynArray< THandle< CResource > > & inplaceResources );

BACKEND_DATA_API data::Result SaveResource( const res::ResourcePath path, const THandle< CResource >& resource, data::ResourceDepot& depot, data::TargetPlatform platform, const red::DynArray< THandle< CResource > > & inplaceResources, job::Builder& builder );

// Class to adapt the older version of the ICooker interface to the newer version
// In the old version the resource was loaded and saved by the CookerTask where as in the
// newer version the resource loading and saving is done inside the ICooker derived class
class BACKEND_DATA_API BasicCooker : public ICooker
{
	RTTI_DECLARE_TYPE( BasicCooker );
public:
	virtual bool CollectDependencies( const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system ) const override final;
	virtual bool Cook( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const override final;

protected:
	// Old style cooking function to cook a resource with only itself as a dependency
	virtual bool CookLoadedResource( THandle< CResource >& resource, data::Context& context ) const = 0;
};

} // cooker


