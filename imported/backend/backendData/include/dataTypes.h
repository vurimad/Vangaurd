/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

// Contains mostly forward declarations, include dataTypesImpl.h for all definitions

namespace res
{
class ResourcePath;
class ResourceLoader;
class IBackendResourceDepot;
} // res

namespace resdb
{
class Database;
}

namespace data
{
enum class TargetPlatform : Uint32;

// Different depot locations in the data system
enum class DepotLocation : Uint8
{
	Source,	// Depot in Perforce where editor modified resources are stored
	Cooked,	// Output depot location that stores processed/cooked files
	Output,	// Aliased depot location depending on generator, by default it points to Cooked depot
};

// Determines how to get resources from the data cache
enum class CacheStrategy : Uint32
{
	GET_NEVER,	// Never get built resources from the data cache
	GET_EXACT,	// Only get files that match the prerequisite hash
	GET_LATEST,	// Attempt to get latest resources from the data cache
	GET_LATEST_CLIENT_EXACT_SERVER, // Special flag for GI
};

// Determines how to build resources inside of a data task
enum BuildSettings : Uint32
{
	CLIENT = RED_FLAG(0),	// Permit building on the client
	SERVER = RED_FLAG(1),	// Permit building on the server
	ISOLATED = RED_FLAG(4),	// Only allows building in isolation
};

class ErrorHandler;

class WorkerTaskAccumulator;

// Providing our own typedefs for lower level types
using ResourceDepot = ::res::IBackendResourceDepot;
using ResourceLoader = ::res::ResourceLoader;

class Context;

RED_MEMORY_POOL( PoolDataSystem, red::memory::DefaultAllocator, BACKEND_DATA_API );

} // data
