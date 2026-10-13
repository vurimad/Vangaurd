/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourceCommand.h"

namespace interop
{
	class InteropNotifier;
}

namespace res
{
	class IBackendResourceDepot;
}

namespace tools
{
	class EnvironmentPresetsManager;

	class BACKEND_ENGINE_API ResourceImportCommand : public IResourceCommand
	{
	public:
		ResourceImportCommand( 
			const res::ResourcePath& destinationPath, 
			const red::AbsolutePath& sourcePath,
			EnvironmentPresetsManager& envPresetsManager,
			res::IBackendResourceDepot* depot,
			interop::InteropNotifier* optionalThumbnailNotifier = nullptr );
		virtual ~ResourceImportCommand();

		virtual ResourceCommandResult Execute( const THandle< ResourceCommandOptions >& options ) override;
		virtual THandle< ResourceCommandOptions > CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions = nullptr ) const override;

	private:
		THandle< CResource > CreateResource( const rtti::ClassType* resourceClass, String& outMessage ) const;

	private:
		res::IBackendResourceDepot* m_depot;
		interop::InteropNotifier* m_thumbnailNotifier;
		EnvironmentPresetsManager& m_envPresetsManager;

		res::ResourcePath m_resourcePath;
		red::AbsolutePath m_sourcePath;
	};
}