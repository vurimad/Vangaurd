/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourceCommand.h"

namespace tools
{
	class ResourceBackend;

	class ResourceCommandFactory
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		ResourceCommandFactory( ResourceBackend* backend );
		~ResourceCommandFactory();

		ResourceCommandUniquePtr CreateImportCommand( const res::ResourcePath& destinationPath, const red::AbsolutePath& sourcePath );
		ResourceCommandUniquePtr CreateCommand( const CName& commandName, const res::ResourcePath& resPath, const red::DynArray< res::ResourcePath >& additionalPaths );

	private:
		ResourceBackend* m_backend;
	};

} // namespace tools