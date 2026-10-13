
#include "build.h"
#include "binkValidator.h"

#include "../../../common/resource/include/binkResource.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( BinkValidator, tools );
	RTTI_PARENT_TYPE( IValidator );
RTTI_END_TYPE();

namespace tools
{

	BinkValidator::BinkValidator()
		: IValidator(ClassID< Bink >())
	{
	}

	Bool BinkValidator::Validate(const THandle<CResource>& resource, tools::IMessageReporter& errorHandler) const
	{
		return false;
	}
}
