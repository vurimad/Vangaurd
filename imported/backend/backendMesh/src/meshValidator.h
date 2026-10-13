
#pragma once

#include "../../backendEngine/include/validator.h"

namespace tools
{
	class Rule;
	class RuleCondition;

	class MeshValidator : public IValidator
	{
		RTTI_DECLARE_TYPE( MeshValidator );

	public:
		MeshValidator();

		virtual Bool Validate( const THandle<CResource>& resource, tools::IMessageReporter& errorHandler ) const override;

	private:
		const red::HashMap< CName, Rule >& GetRulesMap() const;
		void FillRulesMap() const;

		const red::HashMap< CName, RuleCondition >& GetConditionsMap() const;
		void FillConditionsMap() const;

		static red::HashMap< CName, Rule > s_rulesMap;
		static red::HashMap< CName, RuleCondition > s_rulesConditionsMap;
	};
}