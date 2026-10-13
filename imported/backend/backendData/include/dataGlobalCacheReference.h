/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace data
{

// Flags for how long the global cache should be kept alive for
enum GlobalCacheLifetime : Uint32
{
	GCL_WorldBuild		= RED_FLAG(0),	// Keep alive during world building
	GCL_Cooking			= RED_FLAG(1),	// Keep alive during cooking
	GCL_GenerateIndex	= RED_FLAG(2),	// Keep alive during global index resource generation
	GCL_Always			= RED_FLAG(15),	// Keep alive always (Do not use normally)
};

class BACKEND_DATA_API GlobalCacheReference
{
	RTTI_DECLARE_POLYMORPHIC_TYPE( GlobalCacheReference );
public:
	virtual ~GlobalCacheReference() = 0;

	virtual void Initialise( job::Counter& counter ) = 0;
	virtual void Shutdown( job::Counter& counter ) = 0;

	virtual Uint32 GetLifetime() const = 0;
};

class BACKEND_DATA_API GlobalCacheManager
{
public:
	GlobalCacheManager();
	~GlobalCacheManager();

	GlobalCacheManager( const GlobalCacheManager& ) = delete;
	GlobalCacheManager( GlobalCacheManager&& ) = delete;
	GlobalCacheManager& operator=( const GlobalCacheManager& ) = delete;
	GlobalCacheManager& operator=( GlobalCacheManager&& ) = delete;

	void InitAllGlobalCaches();
	void ShutAllGlobalCaches();
	void ShutGlobalCaches( Uint32 lifetimeMask );

	void InitAllGlobalCaches( job::Counter& counter );
	void ShutAllGlobalCaches( job::Counter& counter );
	void ShutGlobalCaches( Uint32 lifetimeMask, job::Counter& counter );

private:
	red::DynArray< GlobalCacheReference* > m_globalCaches;
	Uint32 m_lifetimesEnded;
	static bool s_initialised;
};

} // data
