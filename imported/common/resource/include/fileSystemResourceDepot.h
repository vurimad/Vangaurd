/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "backendResourceDepot.h"

namespace res
{
namespace prv
{

class DirectoryListingCache;

// Helper depot class that handles real file system based tasks for the resource depots
// A resource depot that uses this class needs to implement three methods below so that
// everything works as intended
class RED_RESOURCE_API FileSystemResourceDepot : public res::IBackendResourceDepot
{
public:
	virtual ~FileSystemResourceDepot() override;

	virtual red::SharedPtr<serialization::IAsyncSource> CreateResourceAsyncSource( const res::ResourcePath resourcePath, const res::CreateResourceAsyncSourceParams& params ) override;
	virtual red::SharedPtr<IFile> CreateResourceWriter( const res::ResourcePath resourcePath ) override;
	virtual red::SharedPtr<IFile> CreateResourceReader( const res::ResourcePath resourcePath, bool followLinks ) override;

	virtual Bool RemoveResource( const res::ResourcePath path ) override;

	virtual bool ResourceExists( const res::ResourcePath resourcePath ) const override;
	virtual bool ResourceIsReadOnly( const res::ResourcePath resourcePath ) const override;
	virtual Uint64 GetResourceTimestamp( const res::ResourcePath resourcePath ) const override;

	virtual res::ResourcePath ResolveResourcePath( const res::ResourcePath resourcePath ) const override;
	virtual bool ResourceIsLink( const res::ResourcePath path ) const override;

	virtual Uint32 Debug_GetResourceTotalSizeOnDisk( const res::ResourcePath resourcePath ) const override;
	virtual Uint32 Debug_GetResourceObjectSizeOnDisk( const res::ResourcePath resourcePath ) const override;

	// Specify that we should use a directory cache for this depot
	void LoadDirectoryListingCache( const red::AbsolutePath& cachePath );

	// Specify a debug name for the log and error messages coming out of this depot
	void SetDebugName( const String& name );

	virtual const red::DynArray< red::String >& GetDLCRootPaths() const override;
	virtual void SetDLCRootPaths( const red::DynArray< String >& rootPaths ) override;
	virtual void ScanForDLCRootPaths() override;

protected:
	FileSystemResourceDepot();

	virtual const char* GetDebugName() const override;

	// Internal function that resolves an existing resource, following links and
	// and other redirections as appropriate.
	// If no existing resource is found then an empty path is returned
	red::AbsolutePath TranslateExistingResourcePath_FollowLinks( const red::StringView& path ) const;

	// Helper function to find directories on disk
	void FindDepotDirectories( const red::AbsolutePath& searchPath, red::DynArray< String >& output ) const;

	// Helper function to find files on disk
	void FindDepotFiles( const red::AbsolutePath& searchPath, const char* pattern, red::DynArray< String >& output ) const;

	// NOTE: Implement these methods in your derived ResourceDepot class

	// Internal function to find a path with an existing resource of that name
	// without following any link files.
	virtual red::AbsolutePath TranslateExistingResourcePath( const red::StringView& path ) const = 0;

	// Internal function to find a path which a resource could be written to
	// without following any link files
	virtual red::AbsolutePath TranslateWritableResourcePath( const red::StringView& path ) const = 0;

private:

	red::AbsolutePath m_directoryCachePath;
	red::UniquePtr< res::prv::DirectoryListingCache > m_directoryCache;
	red::String m_debugName;

	red::DynArray< red::String > m_dlcRootPaths;
};

} // prv
} // res
