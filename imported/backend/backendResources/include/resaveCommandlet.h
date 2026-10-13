/**
 * Copyright c 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/core/include/commandlet.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../../common/resource/include/backendResourceDepot.h"

namespace res
{
class IBackendResourceDepot;
class ResourceEditorDepot;
}

namespace commandlet
{
class FileLinkMap;
class FileLinkResolver;
}

class BACKEND_RESOURCES_API ResaveCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ResaveCommandlet );

public:

	ResaveCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

protected:

	ResaveCommandlet( CName name );

	virtual bool OnResourcePreSave( CResource & resource );
	virtual bool IgnoreFile( const red::AbsolutePath& path );

protected:
	virtual bool ParseOptions( const red::CommandLine& options );
	bool ParseResourcePaths( const red::CommandLine& options );
	bool ParseAbsolutePaths( const red::CommandLine& options );

	bool NeedsResave( const res::ResourcePath& path );
	bool NeedsRelink( const res::ResourcePath& path );
	bool LoadResource_DeconstructedMethod( const res::ResourcePath& path, THandle< CResource >& resource );
	bool LoadResource_ResourceLoaderMethod( const res::ResourcePath& path, THandle< CResource >& resource );
	bool SaveResource( const res::ResourcePath& path, const THandle< CResource >& resource );
	bool ProcessResource( const res::ResourcePath& path );
	void ReportSuccess( const res::ResourcePath& path );
	void ReportFailure( const res::ResourcePath& path );
	Bool CheckResource( const res::ResourcePath& path );

	Bool SpawnLoadingJob( const res::ResourcePath& path );
	Bool PopResourceToLoad();

protected:
	res::ResourceEditorDepot* m_sourceDepot;
	red::UniquePtr< res::IBackendResourceDepot > m_ownedOutputDepot;
	res::IBackendResourceDepot* m_outputDepot;

	red::DynArray< res::ResourcePath > m_resources;
	red::AbsolutePath m_absoluteFile;

	commandlet::FileLinkMap* m_linkMap;
	commandlet::FileLinkResolver* m_linkResolver;

	commandlet::EngineInstance m_engineInstance;

	red::Atomic<Uint32> m_currentResource{0};
	red::Atomic<Uint32> m_failedResource{0};
	red::Atomic<Uint32> m_popResource{0};

	job::Counter m_accumCounter;

	red::AbsolutePath m_outputListingFile;
	bool m_force;
	bool m_relink;
	bool m_dryrun;
	bool m_multithreaded;
	bool m_strict;

	bool m_skipLoadImports;
	bool m_skipPostLoad;
};