/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "../../redMemory/include/sharedPtr.h"
#include "serializationAsyncSource.h"
#include "dlcManifest.h"

#if defined(RED_PLATFORM_ORBIS) || defined(RED_PLATFORM_DURANGO)
#include "../../resource/include/archiveLanguageMap.h"
#endif
namespace serialization
{
	class IAsyncSource;
}

namespace res
{
	class ResourcePath;

	struct CreateResourceAsyncSourceParams
	{
		serialization::RawDiskPosition ioHint;
	};

	// Resource depot is a container for the mounting points that resolves the loading paths into the appropriate sources
	class RED_REFLECTION_API IResourceDepot
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolEngine );

	public:
		virtual ~IResourceDepot() = 0;

		// Create an async source to access the resource core data
		// NOTE: path may be hash-only path in final mode
		red::SharedPtr<serialization::IAsyncSource> CreateResourceAsyncSource( const ResourcePath resourcePath );
		virtual red::SharedPtr<serialization::IAsyncSource> CreateResourceAsyncSource( const ResourcePath resourcePath, const CreateResourceAsyncSourceParams& params ) = 0;

		// Check if a resource exists for a given path in this depot
		virtual bool ResourceExists( const ResourcePath resourcePath ) const = 0;

		// Checks if a resource is read only for a given path in this depot
		// NOTE: some depots are read only by default and will return true for all files
		virtual bool ResourceIsReadOnly( const ResourcePath resourcePath ) const = 0;

		// Get the timestamp of a depot file
		virtual Uint64 GetResourceTimestamp( const ResourcePath resourcePath ) const = 0;

		// Returns the resolved resource path for the given input resource path
		// The resolved path is one that has all links and other redirections resolved
		// so that it points to the actual resource that will be loaded if a reader is created for it
		virtual res::ResourcePath ResolveResourcePath( const res::ResourcePath resourcePath ) const = 0;

		virtual void ResolveResourcePathBulk(const red::ArraySpan<res::ResourcePath>& inOutResourcePaths) const;

		// Returns the debug name of this Resource Depot to be used in log messages and assertions
		virtual const char* GetDebugName() const = 0;

		virtual Uint32 Debug_GetResourceTotalSizeOnDisk( const res::ResourcePath resourcePath ) const = 0;
		virtual Uint32 Debug_GetResourceObjectSizeOnDisk( const res::ResourcePath resourcePath ) const = 0;

		virtual bool IsLanguageVoicePackAvailable( const CName languageCodeName ) const = 0;
		virtual bool IsLanguageVoicePackInstalled( const CName languageCodeName ) const = 0;

		// Returns the root paths found for available DLCs
		virtual const red::DynArray< red::String >& GetDLCRootPaths() const = 0;

#if defined(RED_PLATFORM_ORBIS) || defined(RED_PLATFORM_DURANGO)
		virtual Uint16 GetChunkForVoicePack( res::LanguageID languageID ) = 0;
		virtual LanguagePackStatus GetVoicePackStatus( res::LanguageID languageID ) = 0;
		virtual void ChangeChunksStatus( const red::DynArray< Uint16 >& chunks, LanguagePackStatus status ) = 0;
		virtual bool HaveArchiveMap() const = 0;
		virtual bool IsLanguageChunkID( Uint16 chunkID ) const = 0;
		virtual res::prv::ArchiveLanguageMap* GetLanguageMap() = 0;
		virtual LanguageID GetSystemLanguageID() const = 0;
#endif
	};

} // res

/// resource depot
RED_REFLECTION_API res::IResourceDepot* GetResourceDepot();
RED_REFLECTION_API void SetResourceDepot(res::IResourceDepot* resourceDepot);
