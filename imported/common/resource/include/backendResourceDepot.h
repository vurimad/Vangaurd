/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../redReflection/include/resourceDepot.h"

class IFile;

namespace red
{
class String;
class AbsolutePath;
}

namespace res
{

// Interface for editor or backend resource depot, supports writing and directory scanning
class RED_RESOURCE_API IBackendResourceDepot : public res::IResourceDepot
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
public:
	virtual ~IBackendResourceDepot() = default;

	// Create a IFile based reader, can return null if the path is not recognised or invalid
	virtual red::SharedPtr<IFile> CreateResourceReader( const res::ResourcePath resourcePath, bool followLinks = true ) = 0;

	// Create a IFile based writer, can return null if the path is not recognized or invalid
	virtual red::SharedPtr<IFile> CreateResourceWriter( const res::ResourcePath accessPath ) = 0;

	// Scans the depot and returns all subdirectory names of the given directory
	// NOTE: the return strings do not contain trailing slashes, just the names
	virtual void FindDepotSubDirectories( const String& searchDepotPath, red::DynArray< String >& outDirectoryNames ) const = 0;

	// Scans the depot and returns all files in the given directory
	virtual void FindDepotFiles( const red::String& searchDepotPath, const char* pattern, red::DynArray< String >& outFileNames ) const = 0;

	// Remove physical depot file
	virtual bool RemoveResource( const res::ResourcePath path ) = 0;

	virtual red::AbsolutePath TranslateResourcePath( const res::ResourcePath resourcePath ) const = 0;
	// Resolve the physical path that would own a write. The default matches the
	// ordinary translation; editor depots override this so an existing mounted
	// override remains the write target instead of silently changing layers.
	virtual red::AbsolutePath ResolveWritableResourcePath( const res::ResourcePath resourcePath ) const;
	virtual res::ResourcePath TranslateAbsolutePath( const red::AbsolutePath& absolutePath ) const = 0;

	virtual bool ResourceIsLink( const res::ResourcePath path ) const = 0;

	// Sets the DLC root paths that we should work with
	virtual void SetDLCRootPaths( const red::DynArray< String >& rootPaths ) = 0;

	// Scans the depot looking for all available resource paths
	virtual void ScanForDLCRootPaths() = 0;

	// Functions from IResourceDepot which we will never need a custom implementation for in the editor

	virtual bool IsLanguageVoicePackAvailable( const CName languageCodeName ) const override;
	virtual bool IsLanguageVoicePackInstalled( const CName languageCodeName ) const override;

#if defined(RED_PLATFORM_ORBIS) || defined(RED_PLATFORM_DURANGO)
	virtual Uint16 GetChunkForVoicePack( res::LanguageID languageID ) override final;
	virtual LanguagePackStatus GetVoicePackStatus( res::LanguageID languageID ) override final;
	virtual void ChangeChunksStatus( const red::DynArray< Uint16 >& chunks, LanguagePackStatus status ) override final;
	virtual bool HaveArchiveMap() const override final;
	virtual bool IsLanguageChunkID( Uint16 chunkID ) const override final;
	virtual res::prv::ArchiveLanguageMap* GetLanguageMap() override final;
	virtual LanguageID GetSystemLanguageID() const override final;
#endif
};

} // res
