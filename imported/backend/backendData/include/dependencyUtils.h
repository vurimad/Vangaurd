/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace resdb
{
class Database;
}

namespace data
{
class TaskSystem;
enum class TargetPlatform : Uint32;
}

namespace dependency
{
class Database;

// Recursively look up dependencies in the given reosurces
BACKEND_DATA_API red::DynArray< res::ResourcePath > LookupRecursive( resdb::Database& database, const red::DynArray< res::ResourcePath >& resources );

// Recursively look up hard dependencies only (one's stored in TResRefs) in the given resources
BACKEND_DATA_API red::DynArray< res::ResourcePath > LookupRecursiveHard( resdb::Database& database, const red::DynArray< res::ResourcePath >& resources );

// TEMP HACK for meshes
BACKEND_DATA_API red::DynArray< res::ResourcePath > LookupRecursiveJustRight_HackForCertainCookedMeshesOnly( resdb::Database& database, const res::ResourcePath& resource );

BACKEND_DATA_API red::DynArray< res::ResourcePath > CollectMergeableDependencies( const res::ResourcePath& path, data::TargetPlatform platform, resdb::Database& database, bool includeSoftImports = false );

BACKEND_DATA_API red::DynArray< res::ResourcePath > GetNonInplace( resdb::Database& database, const res::ResourcePath path );

BACKEND_DATA_API red::DynArray< res::ResourcePath > GetResourcesFromGame();
BACKEND_DATA_API red::DynArray< res::ResourcePath > GetResourcesFromScripts();
BACKEND_DATA_API red::DynArray< res::ResourcePath > GetResourcesFromTweakDB();

BACKEND_DATA_API bool InplaceTopologicalSort( resdb::Database& database, red::DynArray< res::ResourcePath >& resources );
BACKEND_DATA_API bool InplaceTopologicalSort( resdb::Database& database, red::DynArray< THandle< CResource > >& resources );

enum class InplaceType
{
	AnyInplace,		// Resources that are referenced at least once inplace
	OnlyInplace,	// Resources that are only ever referenced inplace
	NeverInplace,	// Resources that are never referenced inplace
};

// Find resource paths which are referenced as in place resources inside other resources

BACKEND_DATA_API red::DynArray< res::ResourcePath > GetInplaceResourcePaths( resdb::Database& database, InplaceType type );

namespace prv
{

BACKEND_DATA_API red::String ComputeRelativeHashPath( const res::ResourcePath& resourcePath, const char* extension );

BACKEND_DATA_API bool DumpDebugInfo( const data::TaskSystem& taskSystem, const red::AbsolutePath& basePath );
BACKEND_DATA_API bool DumpDebugInfo( const Database& database, const red::AbsolutePath& basePath );

BACKEND_DATA_API bool ExportDependenciesToJson( const Database& database, const red::AbsolutePath& basePath, const red::DynArray< res::ResourcePath >& paths );

} // prv

} // dependency
