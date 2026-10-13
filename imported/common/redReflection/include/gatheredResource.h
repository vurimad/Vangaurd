/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "resourcePath.h"
#include "handle.h"
#include "resource.h"
#include "rttiClass.h"

// Flags that affect how gathered resources are loaded in the engine and editor and how they are cooked for distribution
// By default gathered resources are cooked, loaded at runtime during BaseEngine init
enum GatheredResourceFlags : Uint32
{
	GRF_Default = 0,
	GRF_Backend = RED_FLAG(0),			// Resource is only needed by the backend/editors (implies GRF_NotCooked)
	GRF_SkipOnHeadless = RED_FLAG(1),	// Resource will be skipped when running server or a headless configuration
	GRF_Fallback = RED_FLAG(2),			// Resource is a fallback resource and should not be released/distributed but is needed now (we may distribute these)
	GRF_Debug = RED_FLAG(3),			// Resource is used for debugging and will not be released/distributed but is needed now
	GRF_Startup = RED_FLAG(4),			// Resource is loaded on startup
	GRF_NotCooked = RED_FLAG(8),		// (Internal) Resource is not cooked and not released/distributed
};

namespace res
{
class ResourceToken;
typedef red::SharedPtr< ResourceToken > ResourceTokenHandle;

// Represents a resource path in code which is tracked for cooking and production purposes
class RED_REFLECTION_API GatheredResourceReference
{
public:
	GatheredResourceReference( const res::ResourcePath path, Uint32 flags );
	~GatheredResourceReference();

	const res::ResourcePath& GetPath() const;

	RED_INLINE bool IsEngine() const { return (m_flags & GRF_Backend) == 0; }
	RED_INLINE bool IsBackend() const { return (m_flags & GRF_Backend) != 0; }
	RED_INLINE bool IsHeadless() const { return (m_flags & GRF_SkipOnHeadless) == 0; }
	RED_INLINE bool IsCooked() const { return (m_flags & GRF_NotCooked) == 0; }
	RED_INLINE bool IsFallback() const { return (m_flags & GRF_Fallback) != 0; }
	RED_INLINE bool IsDebug() const { return (m_flags & GRF_Debug) != 0; }
	RED_INLINE bool IsLoadedOnStartup() const { return (m_flags & GRF_Startup) != 0; }
	RED_INLINE bool IsDelayLoaded() const { return (m_flags & GRF_Startup) == 0; }
	RED_INLINE bool IsNormal() const { return (m_flags & (GRF_Backend | GRF_Debug | GRF_Fallback)) == 0; }

	// Only for the GatheredResource loading system
	Uint32 Internal_GetFlags() const;
	void Internal_SetFlags( Uint32 flags );

protected:
	res::ResourcePath m_path;
	Uint32 m_flags;
};

// Represents a resource in code which can be loaded directly, such as a fallback resource
class RED_REFLECTION_API GatheredResource : public GatheredResourceReference
{
public:
	GatheredResource( const res::ResourcePath path, Uint32 flags );
	~GatheredResource();

	const THandle< CResource >& Get() const;
	const THandle< CResource >& GetSafe() const;
	bool IsLoaded() const;

	res::ResourceTokenHandle IssueLoadingRequest( io::EAsyncPriority priority = io::eAsyncPriority_Normal );
	res::ResourceTokenHandle IssueLoadingRequest( const LoadingOptions& options, io::EAsyncPriority priority = io::eAsyncPriority_Normal );

	void Internal_EnsureLoaded();
	void Internal_Load( job::Counter& accumulator );
	void Internal_Release();

	const res::ResourceTokenHandle& Internal_GetLoadingToken() const;

private:
	res::ResourceTokenHandle m_token;
};

// Static resource of given type, the type T is a resource type
// This is just a wrapper around the GatheredResource so there's less explicit casting to do
template < typename T >
class TStaticResource : public GatheredResource
{
public:
	TStaticResource( const char* path, Uint32 flags )
		: GatheredResource( ResourcePath::Build( path ), flags )
	{}

	TStaticResource( const res::ResourcePath path, Uint32 flags )
		: GatheredResource( path, flags )
	{}

	T* operator->()
	{
		return GetPtr();
	}

	THandle< T > Get()
	{
		return SafeCast< T >( GatheredResource::GetSafe() );
	}

	THandle< T > TryGet()
	{
		return Cast< T >( GatheredResource::Get() );
	}

	T* GetPtr()
	{
		const auto& ptr = GatheredResource::GetSafe();
		return static_cast< T* >( ptr.Get() );
	}
};

namespace prv
{

enum GatheredResourceLoadFlags : Uint32
{
	GRLF_None = 0,
	GRLF_OnlyHeadless = RED_FLAG(0),	// Skip loading resources marked as GRF_SkipOnHeadless
	GRLF_SkipGameRoot = RED_FLAG(1),	// Skip loading startup resources from the game content root
};

// Load gathered resources not marked as GRF_Backend
RED_REFLECTION_API bool LoadEngineGatheredResources( Uint32 flags = GRLF_None );

// Load gathered resources marked as GRF_Backend
RED_REFLECTION_API bool LoadBackendGatheredResources( Uint32 flags = GRLF_None );

// Release all loaded gathered resources
RED_REFLECTION_API void ReleaseAllGatheredResources();

// Returns an array of GatheredResources that failed to load
RED_REFLECTION_API const red::DynArray< red::String >& GetGatheredResourceErrors();

RED_REFLECTION_API red::DynArray< GatheredResource* > GetAllGatheredResources();

#ifndef RED_CONFIGURATION_FINAL
RED_REFLECTION_API red::DynArray< GatheredResourceReference* > GetAllGatheredResourceRefernces();

// Return a list of resource paths for gathered resources that should be cooked
RED_REFLECTION_API red::DynArray< res::ResourcePath > GetCookedGatheredResourcePaths();

// Returns a list of resource paths for gathered resources that need to be available at startup time
// Note that this implies it is a cooked resource as well
RED_REFLECTION_API red::DynArray< res::ResourcePath > GetStartupGatheredResourcePaths();
#endif

} // prv

} // res
