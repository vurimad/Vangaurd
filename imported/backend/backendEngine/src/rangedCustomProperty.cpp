/**
* Copyright (C)2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editableCollectionCustomPropertyEditor.h"
#include "editableCollectionInterop.h"

// Custom editor fro choosing a value in the range. 
// Similar to `.range` property, but instead of hard-coded values property name can be provided in order to limit a value in between other property value
// Make sure appropriate NotifyPropertyChanged are sent to refresh the control and values are clamped OnPostChanged

// Example usage RTTI_PROPERTY( m_maxStreamingDistance ).editable().customEditor( "Ranged;0.01;streamingDistance2" );

namespace tools
{
	static const String c_customTypeID( "Ranged" );
	static const String c_customTypeNameSeparator( ';' );

	class RangedCustomProperty: public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( RangedCustomProperty );

	public:
		virtual void Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root,
			const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly ) override;

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override;
		virtual CName GetControlType() const override;

		virtual Bool HasUnlistedChildren() const override { return true; }
		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override;
	
	private:

		String m_minToken;
		String m_maxToken;
	};

	void RangedCustomProperty::Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, 
		const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly )
	{
		IEditableCustomProperty::Initialize( customPropertyType, collection, root, pathSoFar, type, isInlined, isReadOnly );

		String customTypeNameString( customPropertyType.AsChar() );
		red::DynArray< String > customTypeNameTokens = customTypeNameString.Split( c_customTypeNameSeparator );

		if ( customTypeNameTokens.Size() == 3u )
		{
			m_minToken = customTypeNameTokens[ 1 ];
			m_maxToken = customTypeNameTokens[ 2 ];
		}
	}

	const Bool RangedCustomProperty::HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const
	{
		String customTypeNameString( customTypeName.AsChar() );
		return customTypeNameString.BeginsWith( c_customTypeID );

	}

	CName RangedCustomProperty::GetControlType() const
	{
		return RED_NAME( String::Printf( "%hs:Ranged", GetType()->GetName().AsChar() ).AsChar() );
	}

	interop::ObjectPtr RangedCustomProperty::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
	{
		if ( name == "_min" )
		{
			return interop::ObjectPtr( RED_NEW( interop::utils::ValueObject< Double > ) ( [ this ] ( interop::IContext& ctx, Double& outValue )
			{
				outValue = 0.0f;
				if ( !::FromStringT< Double >( m_minToken, outValue ) )
				{
					rtti::ValuePtr value;
					if ( !m_minToken.Empty() && GetRoot()->ReadValue( IRTTIContext::GetDefault(), rtti::AccessPath( m_minToken ), value ) )
					{
						if ( value->IsSingle() )
						{
							::FromStringT< Double >( value->GetSingleValue(), outValue );
						}
					}
				}
			} ) );
		}
		else if ( name == "_max" )
		{
			return interop::ObjectPtr( RED_NEW( interop::utils::ValueObject< Double > ) ( [ this ] ( interop::IContext& ctx, Double& outValue )
			{
				outValue = 1.0f;
				if ( !::FromStringT< Double >( m_maxToken, outValue ) )
				{
					rtti::ValuePtr value;
					if ( !m_maxToken.Empty() && GetRoot()->ReadValue( IRTTIContext::GetDefault(), rtti::AccessPath( m_maxToken ), value ) )
					{
						if ( value->IsSingle() )
						{
							::FromStringT< Double >( value->GetSingleValue(), outValue );
						}
					}
				}
			} ) );
		}
		else if ( name == "_useSlider" )
		{
			return interop::ObjectPtr( RED_NEW( interop::utils::ValueObject< Bool > ) ( [ this ] ( interop::IContext& ctx, Bool& outValue )
			{
				outValue = true;
			} ) );
		}

		return IEditableCustomProperty::GetUnlistedChild( ctx, name );
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( RangedCustomProperty, tools );
RTTI_PARENT_TYPE( IEditableCustomProperty );
RTTI_END_TYPE();
