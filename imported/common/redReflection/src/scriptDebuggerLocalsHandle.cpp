/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptDebuggerLocalsHandle.h"
#include "scriptDebuggerLocalsObject.h"

namespace script { namespace debug {

Handle::Handle( const SerializableHandle* handle, const rtti::IType* type, const String& name )
	: m_handle( handle )
	, m_type( type )
	, m_name( name )
{
}

red::DynArray< IVariablePtr > Handle::EnumerateChildren()
{
	red::DynArray< IVariablePtr > out{ red::PoolDebug() };

	if( m_handle->Get() )
	{
		IVariablePtr ptr( RED_NEW( AliasedObject )( m_handle->Get(), "object" ) );
		out.PushBack( std::move( ptr ) );

#ifndef RED_CONFIGURATION_FINAL
		out.PushBack( red::CreateUniquePtr< NativeClassView >( m_handle->Get(), m_handle->Get()->GetNativeTypeName() ) );
#endif
	}

	return out;
}

IVariablePtr Handle::FindChild( const red::StringView& name )
{
	if( name == "object" && m_handle->Get() )
	{
		return IVariablePtr( RED_NEW( AliasedObject )( m_handle->Get(), "object" ) );
	}
	else if( name == "__native__" && m_handle->Get() )
	{
#ifndef RED_CONFIGURATION_FINAL
		return red::CreateUniquePtr< NativeClassView >( m_handle->Get(), m_handle->Get()->GetNativeTypeName() );
#endif
	}

	return nullptr;
}

String Handle::GetName() const
{
	return m_name;
}

const rtti::IType* Handle::GetType() const
{
	return m_type;
}

String Handle::GetValue() const
{
	return String::Printf( "0x%p", m_handle );
}

Uint32 Handle::GetAttributes() const
{
	Uint32 attributes = RED_FLAG( Attributes::ReadOnly );

	if( m_handle->Get() )
		attributes |= RED_FLAG( Attributes::IsExpandable );

	return attributes;
}

const void* Handle::GetRaw() const
{
	return m_handle;
}

} } // namespace script { namespace debug {
