#pragma once

#include "../../backendEngine/include/resourceCommand.h"

namespace tools
{
	class OptimizeRoadMeshCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( OptimizeRoadMeshCommand );

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;

		virtual THandle< ResourceCommandOptions > CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions = nullptr ) const override;

	private:
		ResourceCommandResult Optimize() const;
	};
}