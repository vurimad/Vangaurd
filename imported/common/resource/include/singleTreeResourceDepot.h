/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "fileSystemResourceDepot.h"

namespace res
{
namespace prv
{

// A simple implementation of a resource depot to be used in the backend engine only!
// It maps all of the resource paths directly to a single directory tree mounted on
// a single base root path. 
// NOTE: This should only be used for backend code that deals with cooked or archived data.
class RED_RESOURCE_API SingleTreeResourceDepot : public FileSystemResourceDepot
{
public:
	explicit SingleTreeResourceDepot( const red::AbsolutePath& rootPath );
	virtual ~SingleTreeResourceDepot() override;

	virtual void FindDepotSubDirectories( const String& searchDepotPath, red::DynArray< String >& outDirectoryNames ) const override;
	virtual void FindDepotFiles( const String& searchDepotPath, const AnsiChar* pattern, red::DynArray< String >& outFileNames ) const override;

	virtual red::AbsolutePath TranslateResourcePath( const res::ResourcePath resourcePath ) const override;
	virtual res::ResourcePath TranslateAbsolutePath( const red::AbsolutePath& absolutePath ) const override;

private:
	virtual red::AbsolutePath TranslateExistingResourcePath( const red::StringView& path ) const override;
	virtual red::AbsolutePath TranslateWritableResourcePath( const red::StringView& path ) const override;

	const red::AbsolutePath m_rootPath;
};

} // prv
} // res
