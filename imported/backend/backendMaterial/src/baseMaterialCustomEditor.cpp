#include "build.h"

#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"

#include "../../../common/resourceMaterial/include/materialInstance.h"

namespace tools
{
	static const CName s_handledType = RED_NAME_CONSTEXPR( "baseMaterialRef" );

	class ClearParametersFunc : public interop::utils::Functor0< void >
	{
		static void Execute( interop::IContext& ctx, const THandle<CMaterialInstance>& material )
		{
			if ( material )
			{
				material->ClearInstanceParameters();
			}
		}

	public:
		ClearParametersFunc( const THandle<CMaterialInstance>& material )
			: Functor0( [material] ( interop::IContext& ctx )
			{
				Execute( ctx, material );
			} )
		{
		}
	};

	class NotifyParametersChangeFunc : public interop::utils::Functor0< void >
	{
		static void Execute( interop::IContext& ctx, const THandle<ISerializable>& parent )
		{
			if ( parent )
			{
				parent->NotifyPropertyChanged( rtti::AccessPath( "instanceParameters" ) );
			}
		}

	public:
		NotifyParametersChangeFunc( const THandle<ISerializable>& parent )
			: Functor0( [parent] ( interop::IContext& ctx )
			{
				Execute( ctx, parent );
			} )
		{
		}
	};

	class CustomPropertyEditor_BaseMaterial : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( CustomPropertyEditor_BaseMaterial );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			return customTypeName == s_handledType;
		}

		virtual Bool HasUnlistedChildren() const override
		{
			return true;
		}
		
		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override
		{
			if ( name.EqualsNC( "clearParameters" ) )
			{
				if ( m_materialInstance )
				{
					return interop::ObjectPtr( RED_NEW( ClearParametersFunc )( m_materialInstance ) );
				}
				else
				{
					return interop::ObjectPtr( RED_NEW( NotifyParametersChangeFunc )( FindParentObject< ISerializable >() ) );
				}
			}
			return IEditableCustomProperty::GetUnlistedChild( ctx, name );
		}

		virtual CName GetControlType() const override
		{
			return GetType()->GetName();
		}

		virtual void Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly ) override
		{
			IEditableCustomProperty::Initialize( customPropertyType, collection, root, pathSoFar, type, isInlined, isReadOnly );
			m_materialInstance = FindParentObject< CMaterialInstance >();
		}

	private:
		THandle< CMaterialInstance > m_materialInstance;
	};
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( CustomPropertyEditor_BaseMaterial, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();