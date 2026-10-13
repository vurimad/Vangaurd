#pragma once

#include "../../backendEngine/include/resourceCommand.h"

namespace tools
{
	class GenerateComponentsIdsCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( GenerateComponentsIdsCommand );

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;

		virtual THandle< ResourceCommandOptions > CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions = nullptr ) const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;
	};

} // tools
