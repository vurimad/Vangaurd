/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "rttiPropertyOverrideBuilder.h"
#include "rttiProperty.h"
#include "rttiClass.h"

namespace rtti
{
	//////////////////////////////////////////////////////////////////////////
	// PropertyOverrideBuilder
	//////////////////////////////////////////////////////////////////////////
	PropertyOverrideBuilder::PropertyOverrideBuilder( const rtti::ClassType* parentClass, CName name )
		: m_parentClass( parentClass )
		, m_name( name )
		, m_flags( PF_Clear | PF_Reset | PF_Select | PF_Browsable )
		, m_attributeModifiedFlags( 0 )
	{
	}

	void PropertyOverrideBuilder::AddPropertyOverrideToClass()
	{
		Property* property = RED_NEW( Property )(
			nullptr,
			m_parentClass,
			0,
			m_name,
			m_flags | PF_Native,
			m_tooltip
			);

		const_cast<ClassType*>(m_parentClass)->AddPropertyOverride( property, m_attributeModifiedFlags );
	}

	// sets readOnly flag
	PropertyOverrideBuilder& PropertyOverrideBuilder::readOnly()
	{
		RED_FATAL_ASSERT( ( m_flags & PF_Editable ) == 0 || ( m_flags & PF_Inlined ) != 0, "Can't be set to read only when editable flag is set" );
		m_flags |= PF_ReadOnly | PF_Editable;
		m_attributeModifiedFlags |= OA_ReadOnly;
		return *this;
	}

	// clears editable flag
	PropertyOverrideBuilder& PropertyOverrideBuilder::notEditable()
	{
		m_flags &= ~PF_Editable;
		m_attributeModifiedFlags |= OA_Editable;
		return *this;
	}

	PropertyOverrideBuilder& PropertyOverrideBuilder::browsable( Bool isBrowsable )
	{
		if( isBrowsable )
		{
			m_flags |= PF_Browsable;
		}
		else
		{
			m_flags &= ~PF_Browsable;
		}

		m_attributeModifiedFlags |= OA_Browsable;
		return *this;
	}

	PropertyOverrideBuilder& PropertyOverrideBuilder::instanceEditable()
	{
		m_flags |= PF_InstanceEditable;
		m_attributeModifiedFlags |= OA_InstanceEditable;

		return *this;
	}


} // rtti
