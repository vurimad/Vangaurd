/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace res
{
class IBackendResourceDepot;
} // res

namespace dependency
{
namespace prv
{

enum class ScanResult : Uint8
{
	OK,
	NOT_SCANNED,		// Has not been scanned yet
	FILE_NOT_FOUND,		// File could not be opened
	IO_ERROR,			// Failed to read the file
	EMPTY,				// File is empty
	INVALID_VERSION,	// Invalid binary file version
	TABLE_FAILURE,		// Failed to read the binary file table
	PARSE_FAILURE,		// Failed during parsing of file
	NOT_A_RESOURCE,		// Not a resource that is supported by this scanner
	DATA_ERROR,			// General data error while processing resource
	LINK_ERROR,			// Invalid link file
};

constexpr bool Success( ScanResult result ) { return result == ScanResult::OK; }
constexpr bool Failed( ScanResult result ) { return result != ScanResult::OK; }


enum class ScanDependencyType : Uint8
{
	HARD,	// Hard link type which is loaded with the resource (TResRef)
	SOFT,	// Soft link type which can be delay loaded (TResAsyncRef)
};

enum class ScanDependencyLocation : Uint8
{
	PRIMARY,	// Link found in the primary / outer resource
	INNER,	// Link found in an inner resource
};

struct ScanDependency
{
	using Type = ScanDependencyType;
	using Location = ScanDependencyLocation;

	res::ResourcePath path;
	Type type;
	Location location;
	bool inplace;
};

BACKEND_DATA_API ScanResult ScanResource( res::IBackendResourceDepot* depot, const res::ResourcePath path, red::DynArray< res::ResourcePath >& result );
BACKEND_DATA_API ScanResult ScanResource( res::IBackendResourceDepot* depot, const res::ResourcePath path, red::DynArray< ScanDependency >& result );
BACKEND_DATA_API ScanResult ScanResource( red::SharedPtr< IFile > file, const res::ResourcePath path, red::DynArray< ScanDependency >& result );

// Determines if a given string looks like it could be a resource path
// NOTE: This should only be used in commandlets or other statistics gathering code
BACKEND_DATA_API bool LooksLikeResourcePath( const red::StringView& pathStrView );

} // prv
} // dependency
