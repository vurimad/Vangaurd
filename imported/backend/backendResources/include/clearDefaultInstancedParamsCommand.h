#pragma once

#include "../../backendEngine/include/resourceCommand.h"

namespace tools
{
	class ClearDefaultInstancedParams_Command : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( ClearDefaultInstancedParams_Command );

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;

		virtual THandle< ResourceCommandOptions > CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions = nullptr ) const override;

	private:
		ResourceCommandResult ClearDefaultInstancedParams() const;
	};
}