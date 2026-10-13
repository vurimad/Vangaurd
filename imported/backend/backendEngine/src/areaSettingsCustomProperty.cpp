#include "build.h"
#include "editableCollectionCustomPropertyEditor.h"

namespace tools
{
	class AreaSettings_CustomProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( AreaSettings_CustomProperty );
	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			return s_areaSettingsPropertyType == customTypeName;
		}

		virtual CName GetControlType() const override
		{
			return s_areaSettingsPropertyType;
		}

	private:
		static CName s_areaSettingsPropertyType;
	};
	CName AreaSettings_CustomProperty::s_areaSettingsPropertyType = RED_NAME_CONSTEXPR( "AreaSettings" );

}

RTTI_BEGIN_TYPE_IN_NAMESPACE( AreaSettings_CustomProperty, tools );
RTTI_PARENT_TYPE( IEditableCustomProperty );
RTTI_END_TYPE();