
#pragma once

#include "../../backendEngine/include/validator.h"

namespace tools
{
	// Dummy validator used only to let validate commandlet know that bink resources should be validated.
	class BinkValidator : public IValidator
	{
		RTTI_DECLARE_TYPE(BinkValidator);

	public:
		BinkValidator();

		virtual Bool Validate( const THandle<CResource>& resource, tools::IMessageReporter& errorHandler ) const override;

	};
}