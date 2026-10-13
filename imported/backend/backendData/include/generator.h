/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataTypes.h"
#include "dataContext.h"
#include "dependencyCollector.h"

namespace job
{
	class Builder;
}

class BACKEND_DATA_API IGenerator
{
	RTTI_DECLARE_POLYMORPHIC_TYPE( IGenerator );
	RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );

public:
	virtual ~IGenerator() = 0;

	// Query if this generator can generate the specified output resource
	virtual bool CanGenerate( const res::ResourcePath& resourcePath ) const = 0;

	// Initialises the environment for the generator to run in. This is run before CollectDependencies
	// is called and so should only be used to load data that is used in dependency resolution
	virtual bool InitialiseEnvironment( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const;

	// Collect the dependencies for the given resource in the provided collector object
	// This should be a fast and light weight operation and do as little work as possible
	// It should avoid loading the actual resource or doing heavy processing
	// Return true on success or false if any error occurred and generation should be halted
	// NOTE: This will be run as part of a job
	virtual bool CollectDependencies( const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system ) const = 0;

	// Create jobs to generate the given resource using the provided builder
	// When this is called all of the output and intermediate dependencies have been generated
	// and are up to date.
	// The system context is guaranteed to exist and be valid for the entire execution of the jobs
	// NOTE: This will be called from a job
	virtual bool Generate( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const = 0;

	// Returns the version of the generator object
	// This number denotes a particular version of the generator code and the related
	// code that is used in generation of the output resource. Whenever it is changed
	// the system will know that it needs to regenerate the affected files.
	// NOTE: It should be bumped up whenever the output format changes, the internal
	// structure of the data changes, or the code used to do the conversion changes. 
	// NOTE: This is independent of the file version number in each resource.
	virtual Uint32 GetVersion() const = 0;

	// Returns the cache strategy to use for this generator
	// This will determine if the resources built by this generator are grabbed from the data cache
	// and which strategy they use for getting them
	virtual data::CacheStrategy GetCacheStrategy() const;

	// Returns a bitfield of build settings created from the data::BuildSettings enumeration
	virtual Uint32 GetBuildSettings() const;

	// Returns the output depot where the generator will save the primary resource to
	virtual data::DepotLocation GetOutputDepot() const;

	// Returns the name of the generator
	CName GetName() const;

	// Finds the appropriate generator class that can generate the given resource and then
	// returns a new instance of it.
	static red::UniquePtr< IGenerator > Create( const res::ResourcePath& path );
};

// NOT FULLY SUPPORTED YET.
// Implement this interface if Generator support Area B&P. 
// Main purpose is to help figuring out what are the minimal area that need to be generated.
class BACKEND_DATA_API GeneratorAreaDescriptorInterface
{
	RTTI_DECLARE_POLYMORPHIC_TYPE( GeneratorAreaDescriptorInterface );

public:
	virtual ~GeneratorAreaDescriptorInterface() = 0;

	virtual math::Box GetMinimalBound( const math::Box& requestedBound ) const = 0;
};
