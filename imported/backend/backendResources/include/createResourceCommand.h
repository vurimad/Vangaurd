#pragma once

#include "../../backendEngine/include/resourceCommand.h"

class IFactory;

namespace tools
{
	class CreateResourceCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( CreateResourceCommand );

	public:
		virtual Bool SupportsResource(const rtti::ClassType* resourceClass) override;
		virtual CName GetName() const override;

		virtual THandle<ResourceCommandOptions> CreateOptions( const THandle<ResourceCommandOptions> suggestedOptions = nullptr ) const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;

	private:
		IFactory* GetFactory() const;
	};
}