#include "build.h"

#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"

#include "../../../common/resourceMaterial/include/materialInstance.h"

#include "../../../common/physics/include/physXNatives.h"
#include "../../../common/physics/include/physicsMaterialLibrary.h"

namespace tools
{
	/// Custom editor for changing CName property physical defining material name used in CTerrainSetup.
	class CustomPropertyEditor_TerrainSetupPhysicalMaterialName : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( CustomPropertyEditor_TerrainSetupPhysicalMaterialName );

	public:
		CustomPropertyEditor_TerrainSetupPhysicalMaterialName();

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override final;
		virtual Bool HasOptions() const override final;
		virtual const red::DynArray<String>& GetOptions() const override final;
		virtual CName GetControlType() const override final;

	private:
		mutable red::DynArray<red::String> m_materialNames;
	};

	CustomPropertyEditor_TerrainSetupPhysicalMaterialName::CustomPropertyEditor_TerrainSetupPhysicalMaterialName()
		: m_materialNames{ red::PoolBackend() }
	{
	}

	const Bool CustomPropertyEditor_TerrainSetupPhysicalMaterialName::HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* /*root*/ ) const
	{
		return customTypeName == RED_NAME_CONSTEXPR_NOREG( "TerrainSetupPhysicalMaterialName" );
	}

	Bool CustomPropertyEditor_TerrainSetupPhysicalMaterialName::HasOptions() const
	{
		return true;
	}

	const red::DynArray<String>& CustomPropertyEditor_TerrainSetupPhysicalMaterialName::GetOptions() const
	{
		m_materialNames.Clear();
		m_materialNames.PushBack( "default.physmat" );

		physics::PhysicsMaterialLibrary& materialLib = physics::GetMaterialLibrary();
		red::ArraySpan< const CName > materialNames = materialLib.GetMaterialsList();

		for ( const CName materialName : materialNames )
		{
			String materialFilenameStr = materialName.AsStringView().ToString();
			String materialNameStr = red::paths::ExtractFileName( materialFilenameStr );
			m_materialNames.PushBack( materialNameStr );
		}

		return m_materialNames;
	}

	CName CustomPropertyEditor_TerrainSetupPhysicalMaterialName::GetControlType() const
	{
		return RED_NAME( red::String::Printf( "%hs:Restricted", ::GetTypeName<CName>().AsChar() ).AsChar() );
	}

} // tools

RTTI_BEGIN_TYPE_IN_NAMESPACE( CustomPropertyEditor_TerrainSetupPhysicalMaterialName, tools );
	RTTI_PARENT_TYPE( IEditableCustomProperty );
RTTI_END_TYPE();
