#pragma once

#include "../../backendEngine/include/resourceCommand.h"

namespace tools
{
	class GeneratePrefabsOptions;

	// A command that generates prefab resources for given input resources.
	// For now, only meshes are supported as input.
	// For each mesh, there will be a prefab generated containing a single mesh node with the source mesh assigned.
	class GeneratePrefabsCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( GeneratePrefabsCommand );

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;
		virtual THandle<ResourceCommandOptions> CreateOptions( const THandle<ResourceCommandOptions> suggestedOptions = nullptr ) const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;

	private:
		ResourceCommandResult GeneratePrefab( const res::ResourcePath& destinationPath, const GeneratePrefabsOptions& options ) const;
	};
}