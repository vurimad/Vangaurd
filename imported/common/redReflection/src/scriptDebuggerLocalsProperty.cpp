/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "scriptDebuggerLocalsProperty.h"

#include "scriptDebuggerLocalsObject.h"
#include "scriptDebuggerLocalsHandle.h"
#include "scriptDebuggerLocalsWeakHandle.h"
#include "scriptDebuggerLocalsSimple.h"
#include "scriptDebuggerLocalsArray.h"
#include "rttiArrayTypesImpl.h"
#include "rttiProperty.h"

namespace script { namespace debug {

Property::Property( const rtti::IType* type, const void* data, const String& name )
	: m_attributes( 0 )
{
	switch ( type->GetType() )
	{
	case RT_Handle:
		m_resolvedProperty.Reset( RED_NEW( Handle )( static_cast<const SerializableHandle*>( data ), type, name ) );
		break;

	case RT_WeakHandle:
		m_resolvedProperty.Reset( RED_NEW( WeakHandle )( static_cast<const SerializableWeakHandle*>( data ), type, name ) );
		break;

	case RT_Class:
		m_resolvedProperty.Reset( RED_NEW( AliasedObject )( data, static_cast<const rtti::ClassType*>( type ), name ) );
		break;

	case RT_Array:
		m_resolvedProperty.Reset( RED_NEW( Array )( data, static_cast<const rtti::ArrayType*>( type ), name ) );
		break;

	default:
		m_resolvedProperty.Reset( RED_NEW( Simple )( data, type, name ) );
	}
}

Property::Property( const rtti::Property* property, const void* data )
	: Property( property->GetType(), data, property->GetName().AsChar() )
{
	if ( property->IsPrivate() )
		m_attributes |= RED_FLAG( Attributes::AccessPrivate );

	else if ( property->IsProtected() )
		m_attributes |= RED_FLAG( Attributes::AccessProtected );

	else if ( property->IsPublic() )
		m_attributes |= RED_FLAG( Attributes::AccessPublic );
}

Property::~Property()
{

}

red::DynArray< IVariablePtr > Property::EnumerateChildren()
{
	return m_resolvedProperty->EnumerateChildren();
}

IVariablePtr Property::FindChild( const red::StringView& name )
{
	return m_resolvedProperty->FindChild( name );
}

String Property::GetName() const
{
	return m_resolvedProperty->GetName();
}

const rtti::IType* Property::GetType() const
{
	return m_resolvedProperty->GetType();
}

String Property::GetValue() const
{
	return m_resolvedProperty->GetValue();
}

Uint32 Property::GetAttributes() const
{
	return m_resolvedProperty->GetAttributes() | m_attributes;
}

const void* Property::GetRaw() const
{
	return m_resolvedProperty->GetRaw();
}

} } // namespace script { namespace debug {
