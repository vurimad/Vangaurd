/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/core/include/commandlet.h"
#include "../../backendEngine/include/commandletEngineInstance.h"

namespace res
{
class IBackendResourceDepot;
class ResourceEditorDepot;
}

class BACKEND_RESOURCES_API FindResourceCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( FindResourceCommandlet );

public:

	FindResourceCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

protected:

	FindResourceCommandlet( CName name );

	virtual bool ParseOptions( const red::CommandLine& options );
	virtual void ApplyResourceLoadingParams( res::IssueLoadingRequestParameter& param ) const;
	virtual bool SupportsResourcePath( const res::ResourcePath& path ) const;
	virtual bool CheckResource( const CResource& resource ) = 0;

private:
	bool ParseResourcePaths( const red::CommandLine& options );
	bool ParseAbsolutePaths( const red::CommandLine& options );

	bool LoadResource( const res::ResourcePath& path, THandle< CResource >& resource );
	bool ProcessResource( const res::ResourcePath& path );
	void ReportMatch( const res::ResourcePath& path );

	res::ResourceEditorDepot* m_sourceDepot;

	red::DynArray< res::ResourcePath > m_resources;
	red::AbsolutePath m_absoluteFile;

	commandlet::EngineInstance m_engineInstance;

	red::AbsolutePath m_outputListingFile;
};